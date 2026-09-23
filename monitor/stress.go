package main

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"time"
)

func stressMode(target string, auth authSettings, payloadBytes, cycles int) error {
	if payloadBytes <= 0 {
		return fmt.Errorf("stress-bytes must be greater than zero")
	}
	if cycles <= 0 {
		return fmt.Errorf("stress-cycles must be greater than zero")
	}

	address, err := normalizeBoardAddress(target)
	if err != nil {
		return err
	}

	payload := make([]byte, payloadBytes)
	received := make([]byte, payloadBytes)

	var totalBytes int64
	started := time.Now()

	for cycle := 1; cycle <= cycles; cycle++ {
		fillStressPayload(payload, cycle)
		clear(received)

		conn, err := dialESPNS(address, dialTimeout, auth)
		if err != nil {
			return fmt.Errorf("stress cycle %d connect to %s: %w", cycle, address, err)
		}

		cycleStarted := time.Now()
		writeErr := make(chan error, 1)
		go func() {
			_, err := conn.Write(payload)
			writeErr <- err
		}()

		_, readErr := io.ReadFull(conn, received)
		if readErr != nil {
			_ = conn.Close()
			return fmt.Errorf("stress cycle %d read: %w", cycle, readErr)
		}
		if err := <-writeErr; err != nil {
			_ = conn.Close()
			return fmt.Errorf("stress cycle %d write: %w", cycle, err)
		}

		if !bytes.Equal(received, payload) {
			_ = conn.Close()
			for i := range payload {
				if payload[i] != received[i] {
					return fmt.Errorf(
						"stress cycle %d data mismatch at byte %d: sent=0x%02x received=0x%02x",
						cycle, i, payload[i], received[i],
					)
				}
			}
			return fmt.Errorf("stress cycle %d data mismatch", cycle)
		}

		if err := conn.Close(); err != nil {
			return fmt.Errorf("stress cycle %d close: %w", cycle, err)
		}

		totalBytes += int64(payloadBytes) * 2
		elapsed := time.Since(cycleStarted)
		mbit := (float64(payloadBytes) * 2 * 8) / elapsed.Seconds() / 1_000_000
		fmt.Fprintf(
			os.Stderr,
			"%s: stress cycle %d/%d OK — %d bytes echoed, %.2f Mbit/s aggregate\n",
			monitorName, cycle, cycles, payloadBytes, mbit,
		)
	}

	elapsed := time.Since(started)
	mbit := (float64(totalBytes) * 8) / elapsed.Seconds() / 1_000_000
	fmt.Fprintf(
		os.Stderr,
		"%s: stress PASS — %d cycles, %d bytes/cycle, %.2f MiB transferred, %.2f Mbit/s aggregate\n",
		monitorName,
		cycles,
		payloadBytes,
		float64(totalBytes)/(1024*1024),
		mbit,
	)
	return nil
}

func fillStressPayload(payload []byte, cycle int) {
	state := uint32(0x9e3779b9) ^ uint32(cycle*0x45d9f3b)
	for i := range payload {
		state ^= state << 13
		state ^= state >> 17
		state ^= state << 5
		payload[i] = byte(state)
	}
}
