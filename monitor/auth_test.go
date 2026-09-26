package main

import (
	"os"
	"path/filepath"
	"strings"
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

func TestLoadAuthSettingsAcceptsUTF8BOM(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	data := append([]byte{0xEF, 0xBB, 0xBF},
		[]byte(`{"authKey":"0123456789abcdef","allowUnauthenticated":false}`)...)
	if err := os.WriteFile(path, data, 0600); err != nil {
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
	if settings.allowUnauthenticated {
		t.Fatal("expected allowUnauthenticated=false")
	}
}

func TestProvisionAuthConfigCreatesAndReusesKey(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	t.Setenv("ESPNS_CONFIG", path)

	first, gotPath, changed, err := provisionAuthConfig(false)
	if err != nil {
		t.Fatal(err)
	}
	if gotPath != path {
		t.Fatalf("config path = %q, want %q", gotPath, path)
	}
	if !changed {
		t.Fatal("first provisioning should create a config")
	}
	if len(first.key) != 64 {
		t.Fatalf("generated key length = %d, want 64", len(first.key))
	}

	second, _, changed, err := provisionAuthConfig(false)
	if err != nil {
		t.Fatal(err)
	}
	if changed {
		t.Fatal("second provisioning must reuse the existing config")
	}
	if string(second.key) != string(first.key) {
		t.Fatal("repair changed the authentication key")
	}
}

func TestRegenerateAuthConfigChangesKey(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	t.Setenv("ESPNS_CONFIG", path)

	first, _, _, err := provisionAuthConfig(false)
	if err != nil {
		t.Fatal(err)
	}
	second, _, changed, err := provisionAuthConfig(true)
	if err != nil {
		t.Fatal(err)
	}
	if !changed {
		t.Fatal("forced regeneration should rewrite config")
	}
	if string(first.key) == string(second.key) {
		t.Fatal("forced regeneration did not change the key")
	}
}

func TestProvisionAuthConfigRespectsExplicitDisabledConfig(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	if err := os.WriteFile(path, []byte("{\n  \"authKey\": \"\",\n  \"allowUnauthenticated\": true\n}\n"), 0600); err != nil {
		t.Fatal(err)
	}
	t.Setenv("ESPNS_CONFIG", path)

	settings, _, changed, err := provisionAuthConfig(false)
	if err != nil {
		t.Fatal(err)
	}
	if changed {
		t.Fatal("existing config should not be replaced")
	}
	if settings.enabled() {
		t.Fatal("explicitly disabled authentication should remain disabled")
	}
}

func TestWriteManagedFirmwareConfig(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "ESPNetworkSerialConfig.h")
	settings := authSettings{key: []byte(`abc"def\ghi0123456789`)}

	written, err := writeManagedFirmwareConfig(path, settings)
	if err != nil {
		t.Fatal(err)
	}
	if !written {
		t.Fatal("expected managed firmware config to be written")
	}

	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	text := string(data)
	if !strings.Contains(text, managedFirmwareConfigMarker) {
		t.Fatal("managed marker missing")
	}
	if !strings.Contains(text, "#define ESPNS_DEFAULT_AUTH_KEY") {
		t.Fatal("default auth macro missing")
	}
	if !strings.Contains(text, `abc\"def\\ghi`) {
		t.Fatalf("C string was not escaped as expected: %s", text)
	}
}

func TestWriteManagedFirmwareConfigRefusesUnmanagedFile(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "ESPNetworkSerialConfig.h")
	if err := os.WriteFile(path, []byte("#pragma once\n#define USER_SETTING 1\n"), 0600); err != nil {
		t.Fatal(err)
	}

	_, err := writeManagedFirmwareConfig(path, authSettings{key: []byte("0123456789abcdef")})
	if err == nil {
		t.Fatal("expected unmanaged config overwrite to fail")
	}
}

func TestDisabledAuthRemovesOnlyManagedFirmwareConfig(t *testing.T) {
	dir := t.TempDir()
	managed := filepath.Join(dir, "managed.h")
	unmanaged := filepath.Join(dir, "unmanaged.h")

	if _, err := writeManagedFirmwareConfig(managed, authSettings{key: []byte("0123456789abcdef")}); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(unmanaged, []byte("#pragma once\n"), 0600); err != nil {
		t.Fatal(err)
	}

	if _, err := writeManagedFirmwareConfig(managed, authSettings{}); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(managed); !os.IsNotExist(err) {
		t.Fatal("managed config should be removed when auth is disabled")
	}

	if _, err := writeManagedFirmwareConfig(unmanaged, authSettings{}); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(unmanaged); err != nil {
		t.Fatal("unmanaged config must be preserved")
	}
}
