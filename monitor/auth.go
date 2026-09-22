package main

import (
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

func loadAuthSettings() (authSettings, error) {
	settings := authSettings{}

	configPath := strings.TrimSpace(os.Getenv("ESPNS_CONFIG"))
	if configPath == "" {
		executable, err := os.Executable()
		if err == nil {
			configPath = filepath.Join(filepath.Dir(executable), "config.json")
		}
	}

	if configPath != "" {
		data, err := os.ReadFile(configPath)
		switch {
		case err == nil:
			var cfg monitorConfigFile
			if err := json.Unmarshal(data, &cfg); err != nil {
				return settings, fmt.Errorf("parse %s: %w", configPath, err)
			}
			settings.key = []byte(cfg.AuthKey)
			settings.allowUnauthenticated = cfg.AllowUnauthenticated
			settings.source = configPath
		case errors.Is(err, os.ErrNotExist):
			// config.json is optional.
		default:
			return settings, fmt.Errorf("read %s: %w", configPath, err)
		}
	}

	if envKey, ok := os.LookupEnv("ESPNS_AUTH_KEY"); ok {
		settings.key = []byte(envKey)
		settings.source = "ESPNS_AUTH_KEY environment variable"
	}
	if envAllow, ok := os.LookupEnv("ESPNS_ALLOW_UNAUTHENTICATED"); ok {
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
