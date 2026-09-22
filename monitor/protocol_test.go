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

func TestESPNSHandshakeUnauthenticated(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()

	done := make(chan error, 1)
	go func() {
		reader := bufio.NewReader(server)
		hello, err := reader.ReadString('\n')
		if err != nil {
			done <- err
			return
		}
		if !strings.HasPrefix(hello, "ESPNS/1 HELLO nonce=") {
			done <- fmt.Errorf("unexpected hello %q", hello)
			return
		}
		_, err = io.WriteString(server, espnsOKLine+" auth=none mode=raw\n")
		done <- err
	}()

	if err := performESPNSHandshake(client, authSettings{}); err != nil {
		t.Fatalf("handshake failed: %v", err)
	}
	if err := <-done; err != nil {
		t.Fatalf("server side failed: %v", err)
	}
}

func TestESPNSHandshakeRejectsUnauthenticatedDowngrade(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()

	go func() {
		reader := bufio.NewReader(server)
		_, _ = reader.ReadString('\n')
		_, _ = io.WriteString(server, espnsOKLine+" auth=none mode=raw\n")
	}()

	auth := authSettings{key: []byte("0123456789abcdef")}
	if err := performESPNSHandshake(client, auth); err == nil {
		t.Fatal("expected authenticated host to reject auth=none device")
	}
}

func TestESPNSMutualHMACHandshake(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()

	auth := authSettings{key: []byte("0123456789abcdef0123456789abcdef")}
	done := make(chan error, 1)

	go func() {
		reader := bufio.NewReader(server)
		hello, err := reader.ReadString('\n')
		if err != nil {
			done <- err
			return
		}

		fields := parseESPNSFields(strings.TrimSpace(hello))
		clientNonce := fields["nonce"]
		if len(clientNonce) != espnsNonceBytes*2 {
			done <- fmt.Errorf("invalid client nonce %q", clientNonce)
			return
		}

		serverNonce := "00112233445566778899aabbccddeeff"
		serverProof := espnsHMAC(auth.key, "SERVER", clientNonce, serverNonce)

		if _, err := io.WriteString(
			server,
			espnsChallengeLine+
				" auth=hmac-sha256 nonce="+serverNonce+
				" proof="+hex.EncodeToString(serverProof)+
				" mode=raw\n",
		); err != nil {
			done <- err
			return
		}

		authLine, err := reader.ReadString('\n')
		if err != nil {
			done <- err
			return
		}
		authFields := parseESPNSFields(strings.TrimSpace(authLine))
		gotProof, err := hex.DecodeString(authFields["proof"])
		if err != nil {
			done <- err
			return
		}
		wantProof := espnsHMAC(auth.key, "CLIENT", clientNonce, serverNonce)
		if !hmac.Equal(gotProof, wantProof) {
			done <- fmt.Errorf("client proof mismatch")
			return
		}

		_, err = io.WriteString(server, espnsOKLine+" auth=hmac-sha256 mode=raw\n")
		done <- err
	}()

	if err := performESPNSHandshake(client, auth); err != nil {
		t.Fatalf("mutual HMAC handshake failed: %v", err)
	}
	if err := <-done; err != nil {
		t.Fatalf("server side failed: %v", err)
	}
}

func TestESPNSHandshakeRejectsWrongServerProof(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()

	auth := authSettings{key: []byte("0123456789abcdef0123456789abcdef")}

	go func() {
		reader := bufio.NewReader(server)
		hello, _ := reader.ReadString('\n')
		clientNonce := parseESPNSFields(strings.TrimSpace(hello))["nonce"]
		_, _ = io.WriteString(
			server,
			espnsChallengeLine+
				" auth=hmac-sha256 nonce=00112233445566778899aabbccddeeff"+
				" proof="+strings.Repeat("00", 32)+
				" mode=raw\n",
		)
		_ = clientNonce
	}()

	if err := performESPNSHandshake(client, auth); err == nil {
		t.Fatal("expected invalid server proof to fail")
	}
}

func TestESPNSHandshakeRejectsWrongService(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()

	go func() {
		reader := bufio.NewReader(server)
		_, _ = reader.ReadString('\n')
		_, _ = io.WriteString(server, "HTTP/1.1 200 OK\n")
	}()

	if err := performESPNSHandshake(client, authSettings{}); err == nil {
		t.Fatal("expected invalid handshake to fail")
	}
}
