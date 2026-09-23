package main

import (
	"bufio"
	"crypto/hmac"
	"encoding/hex"
	"fmt"
	"io"
	"net"
	"strings"
	"testing"
)

func TestStressModeAuthenticatedEchoAcrossFreshSessions(t *testing.T) {
	const (
		cycles       = 3
		payloadBytes = 32 * 1024
	)

	auth := authSettings{
		key: []byte("0123456789abcdef0123456789abcdef"),
	}

	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()

	serverErr := make(chan error, 1)
	go func() {
		for cycle := 0; cycle < cycles; cycle++ {
			conn, err := listener.Accept()
			if err != nil {
				serverErr <- err
				return
			}

			if err := serveStressTestSession(conn, auth.key, cycle, payloadBytes); err != nil {
				_ = conn.Close()
				serverErr <- err
				return
			}
			_ = conn.Close()
		}
		serverErr <- nil
	}()

	if err := stressMode(listener.Addr().String(), auth, payloadBytes, cycles); err != nil {
		t.Fatal(err)
	}

	if err := <-serverErr; err != nil {
		t.Fatal(err)
	}
}

func serveStressTestSession(conn net.Conn, key []byte, cycle, payloadBytes int) error {
	reader := bufio.NewReader(conn)

	hello, err := reader.ReadString('\n')
	if err != nil {
		return err
	}
	helloFields := parseESPNSFields(strings.TrimSpace(hello))
	clientNonce := helloFields["nonce"]
	if len(clientNonce) != espnsNonceBytes*2 {
		return fmt.Errorf("cycle %d: invalid client nonce %q", cycle, clientNonce)
	}

	serverNonce := fmt.Sprintf("%032x", cycle+1)
	serverProof := espnsHMAC(key, "SERVER", clientNonce, serverNonce, espnsSecureMode)

	if _, err := fmt.Fprintf(
		conn,
		"%s auth=hmac-sha256 nonce=%s proof=%s mode=%s\n",
		espnsChallengeLine,
		serverNonce,
		hex.EncodeToString(serverProof),
		espnsSecureMode,
	); err != nil {
		return err
	}

	authLine, err := reader.ReadString('\n')
	if err != nil {
		return err
	}
	authFields := parseESPNSFields(strings.TrimSpace(authLine))
	suppliedProof, err := hex.DecodeString(authFields["proof"])
	if err != nil {
		return err
	}
	expectedProof := espnsHMAC(key, "CLIENT", clientNonce, serverNonce, espnsSecureMode)
	if !hmac.Equal(suppliedProof, expectedProof) {
		return fmt.Errorf("cycle %d: client proof mismatch", cycle)
	}

	keys, err := deriveESPNSKeys(key, clientNonce, serverNonce)
	if err != nil {
		return err
	}

	if _, err := fmt.Fprintf(
		conn,
		"%s auth=hmac-sha256 mode=%s\n",
		espnsOKLine,
		espnsSecureMode,
	); err != nil {
		return err
	}

	secureConn, err := newESPNSSecureConn(
		conn,
		keys.deviceToHostKey,
		keys.hostToDeviceKey,
		keys.deviceToHostNoncePrefix,
		keys.hostToDeviceNoncePrefix,
	)
	if err != nil {
		return err
	}

	_, err = io.CopyN(secureConn, secureConn, int64(payloadBytes))
	return err
}
