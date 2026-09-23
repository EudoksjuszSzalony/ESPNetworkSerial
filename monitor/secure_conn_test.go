package main

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"encoding/binary"
	"encoding/hex"
	"io"
	"net"
	"strings"
	"testing"
)

func testSessionKeys(t *testing.T) espnsSessionKeys {
	t.Helper()

	keys, err := deriveESPNSKeys(
		[]byte("0123456789abcdef0123456789abcdef"),
		"00112233445566778899aabbccddeeff",
		"ffeeddccbbaa99887766554433221100",
	)
	if err != nil {
		t.Fatal(err)
	}
	return keys
}

func testSecurePair(t *testing.T) (net.Conn, net.Conn) {
	t.Helper()

	left, right := net.Pipe()
	keys := testSessionKeys(t)

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
		_ = hostConn.Close()
		t.Fatal(err)
	}

	return hostConn, deviceConn
}

func sealTestRecord(
	t *testing.T,
	key [32]byte,
	noncePrefix [espnsNoncePrefixBytes]byte,
	sequence uint64,
	payload []byte,
) []byte {
	t.Helper()

	block, err := aes.NewCipher(key[:])
	if err != nil {
		t.Fatal(err)
	}
	aead, err := cipher.NewGCM(block)
	if err != nil {
		t.Fatal(err)
	}

	header := make([]byte, espnsRecordHeaderBytes)
	binary.BigEndian.PutUint16(header[0:2], uint16(len(payload)))
	binary.BigEndian.PutUint64(header[2:10], sequence)
	nonce := makeESPNSNonce(noncePrefix, sequence)
	sealed := aead.Seal(nil, nonce[:], payload, header)

	record := make([]byte, 0, len(header)+len(sealed))
	record = append(record, header...)
	record = append(record, sealed...)
	return record
}

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

func TestSecureConnRecordBoundaries(t *testing.T) {
	sizes := []int{1, 2, 15, 16, 17, 255, 256, 1023, 1024, 1025, 4096}

	for _, size := range sizes {
		t.Run(strings.ReplaceAll("bytes_"+string(rune(size)), " ", "_"), func(t *testing.T) {
			hostConn, deviceConn := testSecurePair(t)
			defer hostConn.Close()
			defer deviceConn.Close()

			payload := make([]byte, size)
			for i := range payload {
				payload[i] = byte((i*31 + size) & 0xff)
			}

			errCh := make(chan error, 1)
			go func() {
				_, err := hostConn.Write(payload)
				errCh <- err
			}()

			got := make([]byte, size)
			offset := 0
			scratch := make([]byte, 7)
			for offset < len(got) {
				want := len(got) - offset
				if want > len(scratch) {
					want = len(scratch)
				}
				n, err := deviceConn.Read(scratch[:want])
				if err != nil {
					t.Fatal(err)
				}
				copy(got[offset:], scratch[:n])
				offset += n
			}

			if err := <-errCh; err != nil {
				t.Fatal(err)
			}
			if !bytes.Equal(got, payload) {
				t.Fatalf("round-trip mismatch for %d bytes", size)
			}
		})
	}
}

func TestSecureConnBidirectionalLargeStream(t *testing.T) {
	hostConn, deviceConn := testSecurePair(t)
	defer hostConn.Close()
	defer deviceConn.Close()

	hostPayload := make([]byte, 1024*1024)
	devicePayload := make([]byte, 1024*1024)
	for i := range hostPayload {
		hostPayload[i] = byte((i*17 + 3) & 0xff)
		devicePayload[i] = byte((i*29 + 11) & 0xff)
	}

	hostReceived := make([]byte, len(devicePayload))
	deviceReceived := make([]byte, len(hostPayload))

	errCh := make(chan error, 4)
	go func() {
		_, err := hostConn.Write(hostPayload)
		errCh <- err
	}()
	go func() {
		_, err := deviceConn.Write(devicePayload)
		errCh <- err
	}()
	go func() {
		_, err := io.ReadFull(hostConn, hostReceived)
		errCh <- err
	}()
	go func() {
		_, err := io.ReadFull(deviceConn, deviceReceived)
		errCh <- err
	}()

	for i := 0; i < 4; i++ {
		if err := <-errCh; err != nil {
			t.Fatal(err)
		}
	}

	if !bytes.Equal(hostReceived, devicePayload) {
		t.Fatal("device->host 1 MiB stream mismatch")
	}
	if !bytes.Equal(deviceReceived, hostPayload) {
		t.Fatal("host->device 1 MiB stream mismatch")
	}
}

func TestSecureConnRejectsTamperedCiphertext(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()
	defer right.Close()

	keys := testSessionKeys(t)
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

	record := sealTestRecord(
		t,
		keys.deviceToHostKey,
		keys.deviceToHostNoncePrefix,
		0,
		[]byte("tamper-me"),
	)
	record[espnsRecordHeaderBytes+2] ^= 0x80

	go func() {
		_, _ = right.Write(record)
	}()

	buf := make([]byte, 32)
	if _, err := hostConn.Read(buf); err == nil {
		t.Fatal("expected tampered ciphertext to fail")
	}
}

func TestSecureConnRejectsTamperedAuthenticatedHeader(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()
	defer right.Close()

	keys := testSessionKeys(t)
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

	record := sealTestRecord(
		t,
		keys.deviceToHostKey,
		keys.deviceToHostNoncePrefix,
		0,
		[]byte{0x41, 0x42},
	)
	// Change authenticated length 2 -> 1 while keeping it syntactically valid.
	// GCM must reject the record rather than exposing plaintext.
	record[1] = 1

	go func() {
		_, _ = right.Write(record)
	}()

	buf := make([]byte, 8)
	if _, err := hostConn.Read(buf); err == nil {
		t.Fatal("expected modified authenticated header to fail")
	}
}

func TestSecureConnRejectsReplay(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()
	defer right.Close()

	keys := testSessionKeys(t)
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

	payload := []byte("once")
	record := sealTestRecord(
		t,
		keys.deviceToHostKey,
		keys.deviceToHostNoncePrefix,
		0,
		payload,
	)

	go func() {
		_, _ = right.Write(record)
		_, _ = right.Write(record)
	}()

	got := make([]byte, len(payload))
	if _, err := io.ReadFull(hostConn, got); err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatal("first record payload mismatch")
	}

	buf := make([]byte, 8)
	if _, err := hostConn.Read(buf); err == nil {
		t.Fatal("expected replayed sequence to fail")
	}
}

func TestSecureConnRejectsOutOfOrderRecord(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()
	defer right.Close()

	keys := testSessionKeys(t)
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

	record := sealTestRecord(
		t,
		keys.deviceToHostKey,
		keys.deviceToHostNoncePrefix,
		1,
		[]byte("sequence-one-first"),
	)

	go func() {
		_, _ = right.Write(record)
	}()

	buf := make([]byte, 32)
	if _, err := hostConn.Read(buf); err == nil {
		t.Fatal("expected out-of-order sequence to fail")
	}
}

func TestSecureConnRejectsTruncatedRecord(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()

	keys := testSessionKeys(t)
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

	record := sealTestRecord(
		t,
		keys.deviceToHostKey,
		keys.deviceToHostNoncePrefix,
		0,
		[]byte("truncated"),
	)

	go func() {
		_, _ = right.Write(record[:len(record)-5])
		_ = right.Close()
	}()

	buf := make([]byte, 32)
	if _, err := hostConn.Read(buf); err == nil {
		t.Fatal("expected truncated record to fail")
	}
}

func TestSecureConnRejectsZeroAndOversizedLengths(t *testing.T) {
	for _, length := range []uint16{0, espnsRecordMaxPayload + 1} {
		t.Run("length", func(t *testing.T) {
			left, right := net.Pipe()
			defer left.Close()
			defer right.Close()

			keys := testSessionKeys(t)
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

			header := make([]byte, espnsRecordHeaderBytes)
			binary.BigEndian.PutUint16(header[0:2], length)
			binary.BigEndian.PutUint64(header[2:10], 0)

			go func() {
				_, _ = right.Write(header)
			}()

			buf := make([]byte, 8)
			if _, err := hostConn.Read(buf); err == nil {
				t.Fatalf("expected length %d to be rejected", length)
			}
		})
	}
}

func TestSecureConnRejectsSequenceExhaustion(t *testing.T) {
	hostNet, deviceNet := net.Pipe()
	defer hostNet.Close()
	defer deviceNet.Close()

	keys := testSessionKeys(t)
	connI, err := newESPNSSecureConn(
		hostNet,
		keys.hostToDeviceKey,
		keys.deviceToHostKey,
		keys.hostToDeviceNoncePrefix,
		keys.deviceToHostNoncePrefix,
	)
	if err != nil {
		t.Fatal(err)
	}
	conn := connI.(*espnsSecureConn)

	conn.txSequence = ^uint64(0)
	if _, err := conn.Write([]byte{0x01}); err == nil {
		t.Fatal("expected exhausted TX sequence to fail")
	}

	conn.rxSequence = ^uint64(0)
	header := make([]byte, espnsRecordHeaderBytes)
	binary.BigEndian.PutUint16(header[0:2], 1)
	binary.BigEndian.PutUint64(header[2:10], ^uint64(0))

	go func() {
		_, _ = deviceNet.Write(header)
	}()

	buf := make([]byte, 1)
	if _, err := conn.Read(buf); err == nil {
		t.Fatal("expected exhausted RX sequence to fail")
	}
}
