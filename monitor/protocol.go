package main

import (
	"fmt"
	"io"
	"net"
	"strings"
	"time"
)

const (
	espnsWireVersion       = 1
	espnsHelloLine         = "ESPNS/1 HELLO\n"
	espnsOKLine            = "ESPNS/1 OK"
	espnsHandshakeTimeout  = 2500 * time.Millisecond
	espnsHandshakeMaxBytes = 256
)

func dialESPNS(address string, timeout time.Duration) (net.Conn, error) {
	conn, err := net.DialTimeout("tcp", address, timeout)
	if err != nil {
		return nil, err
	}

	if err := performESPNSHandshake(conn); err != nil {
		_ = conn.Close()
		return nil, err
	}

	return conn, nil
}

func performESPNSHandshake(conn net.Conn) error {
	if err := conn.SetDeadline(time.Now().Add(espnsHandshakeTimeout)); err != nil {
		return fmt.Errorf("set ESPNS handshake deadline: %w", err)
	}

	if _, err := io.WriteString(conn, espnsHelloLine); err != nil {
		return fmt.Errorf("send ESPNS hello: %w", err)
	}

	line := make([]byte, 0, 64)
	var one [1]byte

	for len(line) < espnsHandshakeMaxBytes {
		n, err := conn.Read(one[:])
		if err != nil {
			return fmt.Errorf("read ESPNS handshake: %w", err)
		}
		if n == 0 {
			continue
		}

		if one[0] == '\n' {
			response := strings.TrimSpace(string(line))
			if response != espnsOKLine && !strings.HasPrefix(response, espnsOKLine+" ") {
				return fmt.Errorf("unexpected ESPNS handshake response %q", response)
			}

			if err := conn.SetDeadline(time.Time{}); err != nil {
				return fmt.Errorf("clear ESPNS handshake deadline: %w", err)
			}
			return nil
		}

		if one[0] != '\r' {
			line = append(line, one[0])
		}
	}

	return fmt.Errorf("ESPNS handshake exceeded %d bytes", espnsHandshakeMaxBytes)
}
