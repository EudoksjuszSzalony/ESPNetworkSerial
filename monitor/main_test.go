package main

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"strings"
	"testing"
	"time"
)

func TestNormalizeBoardAddress(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  string
	}{
		{name: "IPv4 default port", input: "192.168.1.50", want: "192.168.1.50:3233"},
		{name: "hostname default port", input: "esp32.local", want: "esp32.local:3233"},
		{name: "explicit port", input: "192.168.1.50:4444", want: "192.168.1.50:4444"},
		{name: "quoted address", input: `"192.168.1.50"`, want: "192.168.1.50:3233"},
		{name: "IPv6 default port", input: "2001:db8::1", want: "[2001:db8::1]:3233"},
		{name: "IPv6 zone default port", input: "fe80::1234%7", want: "[fe80::1234%7]:3233"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := normalizeBoardAddress(tt.input)
			if err != nil {
				t.Fatalf("normalizeBoardAddress(%q) returned error: %v", tt.input, err)
			}
			if got != tt.want {
				t.Fatalf("normalizeBoardAddress(%q) = %q, want %q", tt.input, got, tt.want)
			}
		})
	}
}

func TestNormalizeBoardAddressRejectsEmpty(t *testing.T) {
	if _, err := normalizeBoardAddress(""); err == nil {
		t.Fatal("expected empty address to be rejected")
	}
}

func TestReconnectingTCPReconnectsAfterBoardRestart(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()

	serverErr := make(chan error, 1)
	go func() {
		for _, payload := range []string{"first\n", "second\n"} {
			conn, acceptErr := listener.Accept()
			if acceptErr != nil {
				serverErr <- acceptErr
				return
			}

			reader := bufio.NewReader(conn)
			hello, readErr := reader.ReadString('\n')
			if readErr != nil {
				_ = conn.Close()
				serverErr <- readErr
				return
			}
			if !strings.HasPrefix(hello, "ESPNS/1 HELLO nonce=") {
				_ = conn.Close()
				serverErr <- fmt.Errorf("unexpected ESPNS hello %q", hello)
				return
			}

			if _, writeErr := io.WriteString(conn, espnsOKLine+" auth=none mode=raw\n"); writeErr != nil {
				_ = conn.Close()
				serverErr <- writeErr
				return
			}
			if _, writeErr := io.WriteString(conn, payload); writeErr != nil {
				_ = conn.Close()
				serverErr <- writeErr
				return
			}

			_ = conn.Close()
		}
		serverErr <- nil
	}()

	initial, err := dialESPNS(listener.Addr().String(), time.Second, authSettings{})
	if err != nil {
		t.Fatal(err)
	}

	conn := newReconnectingTCP(listener.Addr().String(), initial, 2*time.Second, authSettings{})
	conn.dialTimeout = 200 * time.Millisecond
	conn.reconnectInterval = 10 * time.Millisecond
	defer conn.Close()

	first := make([]byte, len("first\n"))
	if _, err := io.ReadFull(conn, first); err != nil {
		t.Fatalf("first read failed: %v", err)
	}
	if string(first) != "first\n" {
		t.Fatalf("first payload = %q", first)
	}

	second := make([]byte, len("second\n"))
	if _, err := io.ReadFull(conn, second); err != nil {
		t.Fatalf("reconnected read failed: %v", err)
	}
	if string(second) != "second\n" {
		t.Fatalf("second payload = %q", second)
	}

	if err := <-serverErr; err != nil {
		t.Fatalf("test server failed: %v", err)
	}
}
