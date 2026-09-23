package main

import (
	"bytes"
	"fmt"
	"os"
	"time"
)

func stressMode(target string, auth authSettings, payloadBytes, cycles int, cycleTimeout, cyclePause time.Duration) error {
	if payloadBytes <= 0 {
		return fmt.Errorf("stress-bytes must be greater than zero")
	}
	if cycles <= 0 {
		return fmt.Errorf("stress-cycles must be greater than zero")
	}
	if cycleTimeout <= 0 {
		return fmt.Errorf("stress-timeout must be greater than zero")
	}
	if cyclePause < 0 {
		return fmt.Errorf("stress-pause must not be negative")
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

		fmt.Fprintf(
			os.Stderr,
			"%s: stress cycle %d/%d connecting to %s...\n",
			monitorName, cycle, cycles, address,
		)

		conn, err := dialESPNS(address, dialTimeout, auth)
		if err != nil {
			return fmt.Errorf("stress cycle %d connect to %s: %w", cycle, address, err)
		}

		cycleStarted := time.Now()
		deadline := cycleStarted.Add(cycleTimeout)
		if err := conn.SetDeadline(deadline); err != nil {
			_ = conn.Close()
			return fmt.Errorf("stress cycle %d set deadline: %w", cycle, err)
		}

		fmt.Fprintf(
			os.Stderr,
			"%s: stress cycle %d/%d connected; transferring %d bytes (timeout %s)...\n",
			monitorName, cycle, cycles, payloadBytes, cycleTimeout,
		)

		writeErr := make(chan error, 1)
		go func() {
			_, err := conn.Write(payload)
			if err != nil {
				_ = conn.Close()
			}
			writeErr <- err
		}()

		offset := 0
		nextProgress := 10
		for offset < len(received) {
			n, readErr := conn.Read(received[offset:])
			if n > 0 {
				offset += n
				percent := (offset * 100) / len(received)
				for percent >= nextProgress && nextProgress <= 90 {
					fmt.Fprintf(
						os.Stderr,
						"%s: stress cycle %d/%d progress %d%% (%d/%d bytes echoed)\n",
						monitorName, cycle, cycles, nextProgress, offset, payloadBytes,
					)
					nextProgress += 10
				}
			}
			if readErr != nil {
				_ = conn.Close()
				return fmt.Errorf(
					"stress cycle %d read after %d/%d echoed bytes: %w",
					cycle, offset, payloadBytes, readErr,
				)
			}
			if n == 0 {
				_ = conn.Close()
				return fmt.Errorf(
					"stress cycle %d read made no progress after %d/%d echoed bytes",
					cycle, offset, payloadBytes,
				)
			}
		}

		if err := <-writeErr; err != nil {
			_ = conn.Close()
			return fmt.Errorf("stress cycle %d write: %w", cycle, err)
		}

		if err := conn.SetDeadline(time.Time{}); err != nil {
			_ = conn.Close()
			return fmt.Errorf("stress cycle %d clear deadline: %w", cycle, err)
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

		if cycle < cycles && cyclePause > 0 {
			time.Sleep(cyclePause)
		}
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
