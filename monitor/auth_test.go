package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadAuthSettingsFromExplicitConfig(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	if err := os.WriteFile(path, []byte(`{"authKey":"0123456789abcdef","allowUnauthenticated":true}`), 0600); err != nil {
		t.Fatal(err)
	}

	t.Setenv("ESPNS_CONFIG", path)
	t.Setenv("ESPNS_AUTH_KEY", "")
	t.Setenv("ESPNS_ALLOW_UNAUTHENTICATED", "")

	settings, err := loadAuthSettings()
	if err != nil {
		t.Fatal(err)
	}
	if string(settings.key) != "0123456789abcdef" {
		t.Fatalf("unexpected key %q", settings.key)
	}
	if !settings.allowUnauthenticated {
		t.Fatal("expected allowUnauthenticated=true")
	}
}

func TestLoadAuthSettingsRejectsShortKey(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	if err := os.WriteFile(path, []byte(`{"authKey":"short"}`), 0600); err != nil {
		t.Fatal(err)
	}
	t.Setenv("ESPNS_CONFIG", path)
	t.Setenv("ESPNS_AUTH_KEY", "")

	if _, err := loadAuthSettings(); err == nil {
		t.Fatal("expected short key to fail")
	}
}

func TestGenerateAuthKeyHex(t *testing.T) {
	key, err := generateAuthKeyHex(32)
	if err != nil {
		t.Fatal(err)
	}
	if len(key) != 64 {
		t.Fatalf("generated key length = %d, want 64 hex chars", len(key))
	}
}
