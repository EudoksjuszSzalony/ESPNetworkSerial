package main

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"net"
	"strings"
	"time"
)

const (
	espnsWireVersion       = 1
	espnsOKLine            = "ESPNS/1 OK"
	espnsChallengeLine     = "ESPNS/1 CHALLENGE"
	espnsErrorLine         = "ESPNS/1 ERR"
	espnsHandshakeTimeout  = 2500 * time.Millisecond
	espnsHandshakeMaxBytes = 256
	espnsNonceBytes        = 16
)

func dialESPNS(address string, timeout time.Duration, auth authSettings) (net.Conn, error) {
	conn, err := net.DialTimeout("tcp", address, timeout)
	if err != nil {
		return nil, err
	}

	if err := performESPNSHandshake(conn, auth); err != nil {
		_ = conn.Close()
		return nil, err
	}

	return conn, nil
}

func performESPNSHandshake(conn net.Conn, auth authSettings) error {
	if err := conn.SetDeadline(time.Now().Add(espnsHandshakeTimeout)); err != nil {
		return fmt.Errorf("set ESPNS handshake deadline: %w", err)
	}

	clientNonceRaw := make([]byte, espnsNonceBytes)
	if _, err := rand.Read(clientNonceRaw); err != nil {
		return fmt.Errorf("generate ESPNS client nonce: %w", err)
	}
	clientNonce := hex.EncodeToString(clientNonceRaw)

	if _, err := io.WriteString(conn, "ESPNS/1 HELLO nonce="+clientNonce+"\n"); err != nil {
		return fmt.Errorf("send ESPNS hello: %w", err)
	}

	response, err := readESPNSControlLine(conn)
	if err != nil {
		return fmt.Errorf("read ESPNS handshake: %w", err)
	}

	if response == espnsOKLine || strings.HasPrefix(response, espnsOKLine+" ") {
		fields := parseESPNSFields(response)
		if auth.enabled() && !auth.allowUnauthenticated {
			return fmt.Errorf("device offered unauthenticated ESPNS while an auth key is configured; refusing downgrade")
		}
		if mode := fields["auth"]; mode != "" && mode != "none" {
			return fmt.Errorf("unexpected ESPNS auth mode %q", mode)
		}
		return clearESPNSDeadline(conn)
	}

	if response == espnsErrorLine || strings.HasPrefix(response, espnsErrorLine+" ") {
		return fmt.Errorf("ESPNS device rejected handshake: %s", response)
	}

	if response != espnsChallengeLine && !strings.HasPrefix(response, espnsChallengeLine+" ") {
		return fmt.Errorf("unexpected ESPNS handshake response %q", response)
	}

	fields := parseESPNSFields(response)
	if fields["auth"] != "hmac-sha256" {
		return fmt.Errorf("unsupported ESPNS auth challenge %q", fields["auth"])
	}
	if !auth.enabled() {
		return fmt.Errorf("ESPNS device requires hmac-sha256 authentication but no host auth key is configured")
	}

	serverNonce := fields["nonce"]
	serverProofHex := fields["proof"]
	if len(serverNonce) != espnsNonceBytes*2 {
		return fmt.Errorf("invalid ESPNS server nonce length")
	}
	if _, err := hex.DecodeString(serverNonce); err != nil {
		return fmt.Errorf("invalid ESPNS server nonce: %w", err)
	}
	serverProof, err := hex.DecodeString(serverProofHex)
	if err != nil || len(serverProof) != sha256.Size {
		return fmt.Errorf("invalid ESPNS server proof")
	}

	expectedServerProof := espnsHMAC(auth.key, "SERVER", clientNonce, serverNonce)
	if !hmac.Equal(serverProof, expectedServerProof) {
		return fmt.Errorf("ESPNS server authentication failed")
	}

	clientProof := espnsHMAC(auth.key, "CLIENT", clientNonce, serverNonce)
	if _, err := io.WriteString(conn, "ESPNS/1 AUTH proof="+hex.EncodeToString(clientProof)+"\n"); err != nil {
		return fmt.Errorf("send ESPNS auth proof: %w", err)
	}

	finalResponse, err := readESPNSControlLine(conn)
	if err != nil {
		return fmt.Errorf("read ESPNS auth result: %w", err)
	}
	if finalResponse == espnsErrorLine || strings.HasPrefix(finalResponse, espnsErrorLine+" ") {
		return fmt.Errorf("ESPNS device rejected authentication: %s", finalResponse)
	}
	if finalResponse != espnsOKLine && !strings.HasPrefix(finalResponse, espnsOKLine+" ") {
		return fmt.Errorf("unexpected ESPNS auth result %q", finalResponse)
	}

	finalFields := parseESPNSFields(finalResponse)
	if finalFields["auth"] != "hmac-sha256" {
		return fmt.Errorf("ESPNS authenticated session did not confirm hmac-sha256")
	}

	return clearESPNSDeadline(conn)
}

func espnsHMAC(key []byte, role, clientNonce, serverNonce string) []byte {
	mac := hmac.New(sha256.New, key)
	_, _ = io.WriteString(mac, "ESPNS/1 ")
	_, _ = io.WriteString(mac, role)
	_, _ = io.WriteString(mac, " ")
	_, _ = io.WriteString(mac, clientNonce)
	_, _ = io.WriteString(mac, " ")
	_, _ = io.WriteString(mac, serverNonce)
	return mac.Sum(nil)
}

func readESPNSControlLine(conn net.Conn) (string, error) {
	line := make([]byte, 0, 96)
	var one [1]byte

	for len(line) < espnsHandshakeMaxBytes {
		n, err := conn.Read(one[:])
		if err != nil {
			return "", err
		}
		if n == 0 {
			continue
		}
		if one[0] == '\n' {
			return strings.TrimSpace(string(line)), nil
		}
		if one[0] != '\r' {
			line = append(line, one[0])
		}
	}

	return "", fmt.Errorf("ESPNS handshake exceeded %d bytes", espnsHandshakeMaxBytes)
}

func parseESPNSFields(line string) map[string]string {
	fields := make(map[string]string)
	for _, token := range strings.Fields(line) {
		if i := strings.IndexByte(token, '='); i > 0 {
			fields[token[:i]] = token[i+1:]
		}
	}
	return fields
}

func clearESPNSDeadline(conn net.Conn) error {
	if err := conn.SetDeadline(time.Time{}); err != nil {
		return fmt.Errorf("clear ESPNS handshake deadline: %w", err)
	}
	return nil
}
