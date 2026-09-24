package main

import (
	"bytes"
	"fmt"
	"os"
	"time"
)

type stressOptions struct {
	payloadBytes   int
	cycles         int
	cycleTimeout   time.Duration
	cyclePause     time.Duration
	recover        bool
	recoverTimeout time.Duration
}

func stressMode(target string, auth authSettings, payloadBytes, cycles int, cycleTimeout, cyclePause time.Duration) error {
	return stressModeWithOptions(target, auth, stressOptions{
		payloadBytes: payloadBytes,
		cycles: cycles,
		cycleTimeout: cycleTimeout,
		cyclePause: cyclePause,
	})
}

func stressModeWithOptions(target string, auth authSettings, options stressOptions) error {
	if options.payloadBytes <= 0 { return fmt.Errorf("stress-bytes must be greater than zero") }
	if options.cycles <= 0 { return fmt.Errorf("stress-cycles must be greater than zero") }
	if options.cycleTimeout <= 0 { return fmt.Errorf("stress-timeout must be greater than zero") }
	if options.cyclePause < 0 { return fmt.Errorf("stress-pause must not be negative") }
	if options.recover && options.recoverTimeout <= 0 { return fmt.Errorf("stress-recover-timeout must be greater than zero") }

	address, err := normalizeBoardAddress(target)
	if err != nil { return err }

	payload := make([]byte, options.payloadBytes)
	received := make([]byte, options.payloadBytes)
	var totalBytes int64
	var recoveries int
	started := time.Now()

	for cycle := 1; cycle <= options.cycles; cycle++ {
		fillStressPayload(payload, cycle)
		attempt := 1
		for {
			clear(received)
			fmt.Fprintf(os.Stderr, "%s: stress cycle %d/%d attempt %d connecting to %s...\n", monitorName, cycle, options.cycles, attempt, address)
			conn, err := dialESPNS(address, dialTimeout, auth)
			if err != nil {
				if !options.recover { return fmt.Errorf("stress cycle %d connect to %s: %w", cycle, address, err) }
				if err := waitForStressRecovery(address, auth, options.recoverTimeout); err != nil { return fmt.Errorf("stress cycle %d recovery: %w", cycle, err) }
				recoveries++; attempt++; continue
			}

			cycleStarted := time.Now()
			_ = conn.SetDeadline(cycleStarted.Add(options.cycleTimeout))
			fmt.Fprintf(os.Stderr, "%s: stress cycle %d/%d connected; transferring %d bytes (timeout %s)...\n", monitorName, cycle, options.cycles, options.payloadBytes, options.cycleTimeout)

			writeErr := make(chan error, 1)
			go func() { _, e := conn.Write(payload); if e != nil { _ = conn.Close() }; writeErr <- e }()
			offset, nextProgress := 0, 10
			var transferErr error
			for offset < len(received) {
				n, readErr := conn.Read(received[offset:])
				if n > 0 {
					offset += n
					percent := (offset * 100) / len(received)
					for percent >= nextProgress && nextProgress <= 90 {
						fmt.Fprintf(os.Stderr, "%s: stress cycle %d/%d progress %d%% (%d/%d bytes echoed)\n", monitorName, cycle, options.cycles, nextProgress, offset, options.payloadBytes)
						nextProgress += 10
					}
				}
				if readErr != nil { transferErr = fmt.Errorf("read after %d/%d echoed bytes: %w", offset, options.payloadBytes, readErr); break }
				if n == 0 { transferErr = fmt.Errorf("read made no progress after %d/%d echoed bytes", offset, options.payloadBytes); break }
			}
			if transferErr != nil { _ = conn.Close() }
			werr := <-writeErr
			if transferErr == nil && werr != nil { transferErr = fmt.Errorf("write: %w", werr) }

			if transferErr != nil {
				_ = conn.Close()
				if !options.recover { return fmt.Errorf("stress cycle %d %w", cycle, transferErr) }
				fmt.Fprintf(os.Stderr, "%s: stress cycle %d interrupted (%v); waiting up to %s for a fresh ESPNS session...\n", monitorName, cycle, transferErr, options.recoverTimeout)
				if err := waitForStressRecovery(address, auth, options.recoverTimeout); err != nil { return fmt.Errorf("stress cycle %d recovery: %w", cycle, err) }
				recoveries++; attempt++; continue
			}

			_ = conn.SetDeadline(time.Time{})
			if !bytes.Equal(received, payload) {
				_ = conn.Close()
				return fmt.Errorf("stress cycle %d data mismatch after recovery-safe restart", cycle)
			}
			if err := conn.Close(); err != nil { return fmt.Errorf("stress cycle %d close: %w", cycle, err) }
			totalBytes += int64(options.payloadBytes) * 2
			elapsed := time.Since(cycleStarted)
			mbit := (float64(options.payloadBytes) * 2 * 8) / elapsed.Seconds() / 1_000_000
			fmt.Fprintf(os.Stderr, "%s: stress cycle %d/%d OK — %d bytes echoed, %.2f Mbit/s aggregate\n", monitorName, cycle, options.cycles, options.payloadBytes, mbit)
			break
		}
		if cycle < options.cycles && options.cyclePause > 0 { time.Sleep(options.cyclePause) }
	}

	elapsed := time.Since(started)
	mbit := (float64(totalBytes) * 8) / elapsed.Seconds() / 1_000_000
	fmt.Fprintf(os.Stderr, "%s: stress PASS — %d cycles, %d bytes/cycle, %.2f MiB verified, %d recoveries, %.2f Mbit/s aggregate\n", monitorName, options.cycles, options.payloadBytes, float64(totalBytes)/(1024*1024), recoveries, mbit)
	return nil
}

func waitForStressRecovery(address string, auth authSettings, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for {
		remaining := time.Until(deadline)
		if remaining <= 0 { return fmt.Errorf("device did not establish a fresh authenticated session within %s", timeout) }
		attemptTimeout := dialTimeout
		if remaining < attemptTimeout { attemptTimeout = remaining }
		conn, err := dialESPNS(address, attemptTimeout, auth)
		if err == nil {
			_ = conn.Close()
			fmt.Fprintf(os.Stderr, "%s: device recovered; fresh ESPNS handshake succeeded\n", monitorName)
			return nil
		}
		sleepFor := 250 * time.Millisecond
		if remaining < sleepFor { sleepFor = remaining }
		time.Sleep(sleepFor)
	}
}

func fillStressPayload(payload []byte, cycle int) {
	state := uint32(0x9e3779b9) ^ uint32(cycle*0x45d9f3b)
	for i := range payload {
		state ^= state << 13; state ^= state >> 17; state ^= state << 5
		payload[i] = byte(state)
	}
}
