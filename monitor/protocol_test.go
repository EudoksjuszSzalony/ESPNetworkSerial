package main

import (
	"bufio"
	"io"
	"net"
	"testing"
)

func TestESPNSHandshake(t *testing.T) {
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
		if hello != espnsHelloLine {
			done <- io.ErrUnexpectedEOF
			return
		}
		_, err = io.WriteString(server, espnsOKLine+" auth=none mode=raw\n")
		done <- err
	}()

	if err := performESPNSHandshake(client); err != nil {
		t.Fatalf("handshake failed: %v", err)
	}
	if err := <-done; err != nil {
		t.Fatalf("server side failed: %v", err)
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

	if err := performESPNSHandshake(client); err == nil {
		t.Fatal("expected invalid handshake to fail")
	}
}
