package main

import (
	"bytes"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

const (
	minAuthKeyBytes = 16
	maxAuthKeyBytes = 128

	managedFirmwareConfigMarker = "// ESPNetworkSerial installer-managed configuration"
)

type authSettings struct {
	key                  []byte
	allowUnauthenticated bool
	source               string
}

func (a authSettings) enabled() bool {
	return len(a.key) > 0
}

type monitorConfigFile struct {
	AuthKey              string `json:"authKey"`
	AllowUnauthenticated bool   `json:"allowUnauthenticated"`
}

func resolveAuthConfigPath() string {
	configPath := strings.TrimSpace(os.Getenv("ESPNS_CONFIG"))
	if configPath != "" {
		return configPath
	}

	executable, err := os.Executable()
	if err != nil {
		return ""
	}
	return filepath.Join(filepath.Dir(executable), "config.json")
}

func readMonitorConfig(path string) (monitorConfigFile, bool, error) {
	var cfg monitorConfigFile
	if strings.TrimSpace(path) == "" {
		return cfg, false, errors.New("ESPNS config path is unavailable")
	}

	data, err := os.ReadFile(path)
	switch {
	case err == nil:
		// Windows PowerShell 5.x writes a UTF-8 BOM when using
		// Set-Content -Encoding UTF8. Accept it so a perfectly valid
		// user-created config.json does not make the monitor exit at startup.
		data = bytes.TrimPrefix(data, []byte{0xEF, 0xBB, 0xBF})
		if err := json.Unmarshal(data, &cfg); err != nil {
			return cfg, true, fmt.Errorf("parse %s: %w", path, err)
		}
		return cfg, true, nil
	case errors.Is(err, os.ErrNotExist):
		return cfg, false, nil
	default:
		return cfg, false, fmt.Errorf("read %s: %w", path, err)
	}
}

func settingsFromConfig(path string, cfg monitorConfigFile) (authSettings, error) {
	settings := authSettings{
		key:                  []byte(cfg.AuthKey),
		allowUnauthenticated: cfg.AllowUnauthenticated,
		source:               path,
	}

	if len(settings.key) == 0 {
		settings.source = ""
		return settings, nil
	}
	if len(settings.key) < minAuthKeyBytes {
		return settings, fmt.Errorf("ESPNS auth key is too short: %d bytes; minimum is %d", len(settings.key), minAuthKeyBytes)
	}
	if len(settings.key) > maxAuthKeyBytes {
		return settings, fmt.Errorf("ESPNS auth key is too long: %d bytes; maximum is %d", len(settings.key), maxAuthKeyBytes)
	}

	return settings, nil
}

func loadPersistentAuthSettings() (authSettings, string, error) {
	configPath := resolveAuthConfigPath()
	if configPath == "" {
		return authSettings{}, "", errors.New("ESPNS config path is unavailable")
	}

	cfg, exists, err := readMonitorConfig(configPath)
	if err != nil {
		return authSettings{}, configPath, err
	}
	if !exists {
		return authSettings{}, configPath, nil
	}

	settings, err := settingsFromConfig(configPath, cfg)
	return settings, configPath, err
}

func loadAuthSettings() (authSettings, error) {
	settings := authSettings{}

	configPath := resolveAuthConfigPath()
	if configPath != "" {
		cfg, exists, err := readMonitorConfig(configPath)
		if err != nil {
			return settings, err
		}
		if exists {
			settings, err = settingsFromConfig(configPath, cfg)
			if err != nil {
				return settings, err
			}
		}
	}

	if envKey, ok := os.LookupEnv("ESPNS_AUTH_KEY"); ok && strings.TrimSpace(envKey) != "" {
		settings.key = []byte(envKey)
		settings.source = "ESPNS_AUTH_KEY environment variable"
	}
	if envAllow, ok := os.LookupEnv("ESPNS_ALLOW_UNAUTHENTICATED"); ok && strings.TrimSpace(envAllow) != "" {
		value, err := strconv.ParseBool(envAllow)
		if err != nil {
			return settings, fmt.Errorf("parse ESPNS_ALLOW_UNAUTHENTICATED: %w", err)
		}
		settings.allowUnauthenticated = value
	}

	if len(settings.key) == 0 {
		settings.source = ""
		return settings, nil
	}
	if len(settings.key) < minAuthKeyBytes {
		return settings, fmt.Errorf("ESPNS auth key is too short: %d bytes; minimum is %d", len(settings.key), minAuthKeyBytes)
	}
	if len(settings.key) > maxAuthKeyBytes {
		return settings, fmt.Errorf("ESPNS auth key is too long: %d bytes; maximum is %d", len(settings.key), maxAuthKeyBytes)
	}

	return settings, nil
}

func generateAuthKeyHex(bytes int) (string, error) {
	if bytes <= 0 {
		return "", errors.New("key length must be positive")
	}
	raw := make([]byte, bytes)
	if _, err := rand.Read(raw); err != nil {
		return "", err
	}
	return hex.EncodeToString(raw), nil
}

func writeMonitorConfig(path string, cfg monitorConfigFile) error {
	if strings.TrimSpace(path) == "" {
		return errors.New("ESPNS config path is unavailable")
	}

	parent := filepath.Dir(path)
	if err := os.MkdirAll(parent, 0700); err != nil {
		return fmt.Errorf("create config directory %s: %w", parent, err)
	}

	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return fmt.Errorf("encode %s: %w", path, err)
	}
	data = append(data, '\n')

	tempPath := path + ".tmp"
	if err := os.WriteFile(tempPath, data, 0600); err != nil {
		return fmt.Errorf("write %s: %w", tempPath, err)
	}
	if err := os.Chmod(tempPath, 0600); err != nil && !errors.Is(err, os.ErrPermission) {
		_ = os.Remove(tempPath)
		return fmt.Errorf("secure permissions on %s: %w", tempPath, err)
	}
	if err := os.Rename(tempPath, path); err != nil {
		// os.Rename replaces an existing file on Unix, but Windows may reject
		// that form. Fall back to an explicit remove + rename for key rotation.
		if removeErr := os.Remove(path); removeErr != nil && !errors.Is(removeErr, os.ErrNotExist) {
			_ = os.Remove(tempPath)
			return fmt.Errorf("replace %s: %w", path, err)
		}
		if retryErr := os.Rename(tempPath, path); retryErr != nil {
			_ = os.Remove(tempPath)
			return fmt.Errorf("replace %s: %w", path, retryErr)
		}
	}
	_ = os.Chmod(path, 0600)
	return nil
}

func provisionAuthConfig(forceRegenerate bool) (authSettings, string, bool, error) {
	configPath := resolveAuthConfigPath()
	if configPath == "" {
		return authSettings{}, "", false, errors.New("ESPNS config path is unavailable")
	}

	cfg, exists, err := readMonitorConfig(configPath)
	if err != nil {
		return authSettings{}, configPath, false, err
	}

	if exists && !forceRegenerate {
		settings, err := settingsFromConfig(configPath, cfg)
		return settings, configPath, false, err
	}

	key, err := generateAuthKeyHex(32)
	if err != nil {
		return authSettings{}, configPath, false, fmt.Errorf("generate ESPNS auth key: %w", err)
	}

	cfg = monitorConfigFile{
		AuthKey:              key,
		AllowUnauthenticated: false,
	}
	if err := writeMonitorConfig(configPath, cfg); err != nil {
		return authSettings{}, configPath, false, err
	}

	settings, err := settingsFromConfig(configPath, cfg)
	return settings, configPath, true, err
}

func quoteCStringBytes(data []byte) string {
	var b strings.Builder
	b.WriteByte('"')
	for _, value := range data {
		switch value {
		case '\\':
			b.WriteString("\\\\")
		case '"':
			b.WriteString("\\\"")
		case '\n':
			b.WriteString("\\n")
		case '\r':
			b.WriteString("\\r")
		case '\t':
			b.WriteString("\\t")
		default:
			if value >= 0x20 && value <= 0x7e {
				b.WriteByte(value)
			} else {
				fmt.Fprintf(&b, "\\%03o", value)
			}
		}
	}
	b.WriteByte('"')
	return b.String()
}

func removeManagedFirmwareConfig(path string) (bool, error) {
	data, err := os.ReadFile(path)
	switch {
	case err == nil:
		if !bytes.Contains(data, []byte(managedFirmwareConfigMarker)) {
			return false, nil
		}
		if err := os.Remove(path); err != nil {
			return false, fmt.Errorf("remove managed firmware config %s: %w", path, err)
		}
		return true, nil
	case errors.Is(err, os.ErrNotExist):
		return false, nil
	default:
		return false, fmt.Errorf("read firmware config %s: %w", path, err)
	}
}

func writeManagedFirmwareConfig(path string, settings authSettings) (bool, error) {
	if strings.TrimSpace(path) == "" {
		return false, errors.New("firmware config path is empty")
	}

	if !settings.enabled() {
		_, err := removeManagedFirmwareConfig(path)
		return false, err
	}

	if data, err := os.ReadFile(path); err == nil {
		if !bytes.Contains(data, []byte(managedFirmwareConfigMarker)) {
			return false, fmt.Errorf("refusing to overwrite non-ESPNetworkSerial firmware config: %s", path)
		}
	} else if !errors.Is(err, os.ErrNotExist) {
		return false, fmt.Errorf("read firmware config %s: %w", path, err)
	}

	parent := filepath.Dir(path)
	if err := os.MkdirAll(parent, 0700); err != nil {
		return false, fmt.Errorf("create firmware config directory %s: %w", parent, err)
	}

	content := strings.Join([]string{
		"#pragma once",
		"",
		managedFirmwareConfigMarker + ".",
		"// Generated from the local host config by ESPNetworkSerial Setup.",
		"// This file contains a secret. Do not publish or commit it.",
		"",
		"#define ESPNS_DEFAULT_AUTH_KEY " + quoteCStringBytes(settings.key),
		"",
	}, "\n")

	if err := os.WriteFile(path, []byte(content), 0600); err != nil {
		return false, fmt.Errorf("write firmware config %s: %w", path, err)
	}
	_ = os.Chmod(path, 0600)
	return true, nil
}

func runAuthProvisioning(provision, regenerate bool, firmwareConfigPath string) (bool, error) {
	if !provision && !regenerate && strings.TrimSpace(firmwareConfigPath) == "" {
		return false, nil
	}
	if provision && regenerate {
		return true, errors.New("--provision-auth and --regenerate-auth cannot be used together")
	}

	var (
		settings   authSettings
		configPath string
		changed    bool
		err        error
	)

	if provision || regenerate {
		settings, configPath, changed, err = provisionAuthConfig(regenerate)
	} else {
		settings, configPath, err = loadPersistentAuthSettings()
	}
	if err != nil {
		return true, err
	}

	if provision || regenerate {
		switch {
		case changed && regenerate:
			fmt.Printf("ESPNS authentication: generated a new 256-bit key in %s\n", configPath)
		case changed:
			fmt.Printf("ESPNS authentication: generated a 256-bit key in %s\n", configPath)
		case settings.enabled():
			fmt.Printf("ESPNS authentication: reusing the existing key from %s\n", configPath)
		default:
			fmt.Printf("ESPNS authentication: existing config disables authentication: %s\n", configPath)
		}
	}

	if strings.TrimSpace(firmwareConfigPath) != "" {
		written, err := writeManagedFirmwareConfig(firmwareConfigPath, settings)
		if err != nil {
			return true, err
		}
		if written {
			fmt.Printf("Firmware default authentication config: %s\n", firmwareConfigPath)
		} else if !settings.enabled() {
			fmt.Printf("Firmware default authentication: disabled by host config\n")
		}
	}

	return true, nil
}
