package main

import "testing"

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
