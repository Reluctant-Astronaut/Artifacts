//go:build !windows

package main

import (
	"log"
	"os"
	"path/filepath"
)

func appDataDir() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".config", "bluremote")
}

func ensureInstalled(string) bool { return false }
func doUninstall(dir string)      { _ = os.RemoveAll(dir) }
func keepAwake()                  {}
func openBrowser(u string)        { log.Printf("open %s in a browser", u) }
func closeBrowser()               {}
