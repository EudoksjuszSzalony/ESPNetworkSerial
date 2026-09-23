package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"io"
	"net"
	"sync"
)

const (
	espnsSecureMode        = "aes256-gcm"
	espnsRecordMaxPayload  = 1024
	espnsRecordHeaderBytes = 10
	espnsGCMTagBytes       = 16
	espnsNoncePrefixBytes  = 4
)

type espnsSessionKeys struct {
	hostToDeviceKey         [32]byte
	deviceToHostKey         [32]byte
	hostToDeviceNoncePrefix [espnsNoncePrefixBytes]byte
	deviceToHostNoncePrefix [espnsNoncePrefixBytes]byte
}

func deriveESPNSKeys(authKey []byte, clientNonceHex, serverNonceHex string) (espnsSessionKeys, error) {
	var keys espnsSessionKeys

	clientNonce, err := hex.DecodeString(clientNonceHex)
	if err != nil || len(clientNonce) != espnsNonceBytes {
		return keys, fmt.Errorf("invalid client nonce")
	}
	serverNonce, err := hex.DecodeString(serverNonceHex)
	if err != nil || len(serverNonce) != espnsNonceBytes {
		return keys, fmt.Errorf("invalid server nonce")
	}

	salt := make([]byte, 0, len(clientNonce)+len(serverNonce))
	salt = append(salt, clientNonce...)
	salt = append(salt, serverNonce...)

	prk := hkdfExtractSHA256(salt, authKey)

	copy(keys.hostToDeviceKey[:], hkdfExpandSHA256(prk, []byte("ESPNS/1 aes256-gcm host-to-device key"), len(keys.hostToDeviceKey)))
	copy(keys.deviceToHostKey[:], hkdfExpandSHA256(prk, []byte("ESPNS/1 aes256-gcm device-to-host key"), len(keys.deviceToHostKey)))
	copy(keys.hostToDeviceNoncePrefix[:], hkdfExpandSHA256(prk, []byte("ESPNS/1 aes256-gcm host-to-device nonce-prefix"), len(keys.hostToDeviceNoncePrefix)))
	copy(keys.deviceToHostNoncePrefix[:], hkdfExpandSHA256(prk, []byte("ESPNS/1 aes256-gcm device-to-host nonce-prefix"), len(keys.deviceToHostNoncePrefix)))

	return keys, nil
}

func hkdfExtractSHA256(salt, ikm []byte) []byte {
	mac := hmac.New(sha256.New, salt)
	_, _ = mac.Write(ikm)
	return mac.Sum(nil)
}

func hkdfExpandSHA256(prk, info []byte, length int) []byte {
	if length <= 0 {
		return nil
	}

	result := make([]byte, 0, length)
	var previous []byte
	for counter := byte(1); len(result) < length; counter++ {
		mac := hmac.New(sha256.New, prk)
		_, _ = mac.Write(previous)
		_, _ = mac.Write(info)
		_, _ = mac.Write([]byte{counter})
		previous = mac.Sum(nil)

		remaining := length - len(result)
		if remaining < len(previous) {
			result = append(result, previous[:remaining]...)
		} else {
			result = append(result, previous...)
		}
	}
	return result
}

type espnsSecureConn struct {
	net.Conn

	txAEAD cipher.AEAD
	rxAEAD cipher.AEAD

	txNoncePrefix [espnsNoncePrefixBytes]byte
	rxNoncePrefix [espnsNoncePrefixBytes]byte

	txSequence uint64
	rxSequence uint64

	txMu sync.Mutex
	rxMu sync.Mutex

	rxPlain []byte
}

func newESPNSSecureConn(
	conn net.Conn,
	txKey [32]byte,
	rxKey [32]byte,
	txNoncePrefix [espnsNoncePrefixBytes]byte,
	rxNoncePrefix [espnsNoncePrefixBytes]byte,
) (net.Conn, error) {
	txBlock, err := aes.NewCipher(txKey[:])
	if err != nil {
		return nil, fmt.Errorf("create ESPNS TX AES cipher: %w", err)
	}
	rxBlock, err := aes.NewCipher(rxKey[:])
	if err != nil {
		return nil, fmt.Errorf("create ESPNS RX AES cipher: %w", err)
	}

	txAEAD, err := cipher.NewGCM(txBlock)
	if err != nil {
		return nil, fmt.Errorf("create ESPNS TX GCM: %w", err)
	}
	rxAEAD, err := cipher.NewGCM(rxBlock)
	if err != nil {
		return nil, fmt.Errorf("create ESPNS RX GCM: %w", err)
	}

	if txAEAD.NonceSize() != 12 || rxAEAD.NonceSize() != 12 ||
		txAEAD.Overhead() != espnsGCMTagBytes || rxAEAD.Overhead() != espnsGCMTagBytes {
		return nil, fmt.Errorf("unexpected AES-GCM parameters")
	}

	return &espnsSecureConn{
		Conn:          conn,
		txAEAD:        txAEAD,
		rxAEAD:        rxAEAD,
		txNoncePrefix: txNoncePrefix,
		rxNoncePrefix: rxNoncePrefix,
	}, nil
}

func (c *espnsSecureConn) Write(p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}

	c.txMu.Lock()
	defer c.txMu.Unlock()

	total := 0
	for total < len(p) {
		chunkSize := len(p) - total
		if chunkSize > espnsRecordMaxPayload {
			chunkSize = espnsRecordMaxPayload
		}

		header := make([]byte, espnsRecordHeaderBytes)
		binary.BigEndian.PutUint16(header[0:2], uint16(chunkSize))
		binary.BigEndian.PutUint64(header[2:10], c.txSequence)

		nonce := makeESPNSNonce(c.txNoncePrefix, c.txSequence)
		sealed := c.txAEAD.Seal(nil, nonce[:], p[total:total+chunkSize], header)

		if err := writeAll(c.Conn, header); err != nil {
			return total, err
		}
		if err := writeAll(c.Conn, sealed); err != nil {
			return total, err
		}

		c.txSequence++
		total += chunkSize
	}

	return total, nil
}

func (c *espnsSecureConn) Read(p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}

	c.rxMu.Lock()
	defer c.rxMu.Unlock()

	if len(c.rxPlain) > 0 {
		n := copy(p, c.rxPlain)
		c.rxPlain = c.rxPlain[n:]
		return n, nil
	}

	header := make([]byte, espnsRecordHeaderBytes)
	if _, err := io.ReadFull(c.Conn, header); err != nil {
		return 0, err
	}

	length := int(binary.BigEndian.Uint16(header[0:2]))
	sequence := binary.BigEndian.Uint64(header[2:10])

	if length <= 0 || length > espnsRecordMaxPayload {
		return 0, fmt.Errorf("invalid ESPNS secure record length %d", length)
	}
	if sequence != c.rxSequence {
		return 0, fmt.Errorf("invalid ESPNS secure record sequence %d, expected %d", sequence, c.rxSequence)
	}

	sealed := make([]byte, length+espnsGCMTagBytes)
	if _, err := io.ReadFull(c.Conn, sealed); err != nil {
		return 0, err
	}

	nonce := makeESPNSNonce(c.rxNoncePrefix, sequence)
	plain, err := c.rxAEAD.Open(nil, nonce[:], sealed, header)
	if err != nil {
		return 0, fmt.Errorf("ESPNS secure record authentication failed: %w", err)
	}

	c.rxSequence++
	n := copy(p, plain)
	if n < len(plain) {
		c.rxPlain = append(c.rxPlain[:0], plain[n:]...)
	}
	return n, nil
}

func makeESPNSNonce(prefix [espnsNoncePrefixBytes]byte, sequence uint64) [12]byte {
	var nonce [12]byte
	copy(nonce[0:espnsNoncePrefixBytes], prefix[:])
	binary.BigEndian.PutUint64(nonce[espnsNoncePrefixBytes:], sequence)
	return nonce
}

func writeAll(w io.Writer, p []byte) error {
	for len(p) > 0 {
		n, err := w.Write(p)
		if err != nil {
			return err
		}
		if n <= 0 {
			return io.ErrShortWrite
		}
		p = p[n:]
	}
	return nil
}
