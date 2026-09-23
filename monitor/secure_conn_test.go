package main

import (
	"bytes"
	"encoding/hex"
	"io"
	"net"
	"testing"
)

func TestHKDFSHA256RFC5869Case1(t *testing.T) {
	ikm := bytes.Repeat([]byte{0x0b}, 22)
	salt, _ := hex.DecodeString("000102030405060708090a0b0c")
	info, _ := hex.DecodeString("f0f1f2f3f4f5f6f7f8f9")

	prk := hkdfExtractSHA256(salt, ikm)
	wantPRK, _ := hex.DecodeString("077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5")
	if !bytes.Equal(prk, wantPRK) {
		t.Fatalf("PRK mismatch: %x", prk)
	}

	okm := hkdfExpandSHA256(prk, info, 42)
	wantOKM, _ := hex.DecodeString("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865")
	if !bytes.Equal(okm, wantOKM) {
		t.Fatalf("OKM mismatch: %x", okm)
	}
}

func TestSecureConnBidirectionalAndFragmentedReads(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()
	defer right.Close()

	keys, err := deriveESPNSKeys(
		[]byte("0123456789abcdef0123456789abcdef"),
		"00112233445566778899aabbccddeeff",
		"ffeeddccbbaa99887766554433221100",
	)
	if err != nil {
		t.Fatal(err)
	}

	hostConn, err := newESPNSSecureConn(
		left,
		keys.hostToDeviceKey,
		keys.deviceToHostKey,
		keys.hostToDeviceNoncePrefix,
		keys.deviceToHostNoncePrefix,
	)
	if err != nil {
		t.Fatal(err)
	}

	deviceConn, err := newESPNSSecureConn(
		right,
		keys.deviceToHostKey,
		keys.hostToDeviceKey,
		keys.deviceToHostNoncePrefix,
		keys.hostToDeviceNoncePrefix,
	)
	if err != nil {
		t.Fatal(err)
	}

	hostPayload := bytes.Repeat([]byte("host->device|"), 300)
	devicePayload := bytes.Repeat([]byte("device->host|"), 260)

	errCh := make(chan error, 2)
	go func() {
		_, err := hostConn.Write(hostPayload)
		errCh <- err
	}()
	go func() {
		_, err := deviceConn.Write(devicePayload)
		errCh <- err
	}()

	gotAtDevice := make([]byte, len(hostPayload))
	if _, err := io.ReadFull(deviceConn, gotAtDevice); err != nil {
		t.Fatal(err)
	}
	gotAtHost := make([]byte, len(devicePayload))
	if _, err := io.ReadFull(hostConn, gotAtHost); err != nil {
		t.Fatal(err)
	}

	if !bytes.Equal(gotAtDevice, hostPayload) {
		t.Fatal("host->device plaintext mismatch")
	}
	if !bytes.Equal(gotAtHost, devicePayload) {
		t.Fatal("device->host plaintext mismatch")
	}

	for i := 0; i < 2; i++ {
		if err := <-errCh; err != nil {
			t.Fatal(err)
		}
	}
}

func TestSecureConnRejectsTamperedCiphertext(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()
	defer right.Close()

	keys, err := deriveESPNSKeys(
		[]byte("0123456789abcdef0123456789abcdef"),
		"00112233445566778899aabbccddeeff",
		"ffeeddccbbaa99887766554433221100",
	)
	if err != nil {
		t.Fatal(err)
	}

	hostConn, err := newESPNSSecureConn(
		left,
		keys.hostToDeviceKey,
		keys.deviceToHostKey,
		keys.hostToDeviceNoncePrefix,
		keys.deviceToHostNoncePrefix,
	)
	if err != nil {
		t.Fatal(err)
	}

	go func() {
		header := make([]byte, espnsRecordHeaderBytes)
		header[1] = 1 // length=1, sequence=0
		_, _ = right.Write(header)
		_, _ = right.Write(bytes.Repeat([]byte{0x42}, 1+espnsGCMTagBytes))
	}()

	buf := make([]byte, 1)
	if _, err := hostConn.Read(buf); err == nil {
		t.Fatal("expected tampered secure record to fail")
	}
}
