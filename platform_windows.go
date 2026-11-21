//go:build windows

package main

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"time"
)

const (
	runKey          = `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
	runValue        = "BluRemote"
	createNoWindow  = 0x08000000
	esSystemReq     = 0x00000001
	esDisplayReq    = 0x00000002
	installedExeNam = "BluRemote.exe"
)

var browserProc *os.Process

func hidden(cmd *exec.Cmd) *exec.Cmd {
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: createNoWindow}
	return cmd
}

func appDataDir() string {
	base := os.Getenv("LOCALAPPDATA")
	if base == "" {
		base = os.TempDir()
	}
	return filepath.Join(base, "BluRemote")
}

// ensureInstalled copies the running exe into the app folder and registers
// it to start with Windows. Returns true if this process should exit because
// the installed copy has been launched instead.
func ensureInstalled(dir string) bool {
	self, err := os.Executable()
	if err != nil {
		return false
	}
	target := filepath.Join(dir, installedExeNam)
	if strings.EqualFold(filepath.Clean(self), filepath.Clean(target)) {
		return false // we are the installed copy
	}
	_ = os.MkdirAll(dir, 0o755)

	// Stop an older running copy (but not ourselves) so it can be replaced.
	_ = hidden(exec.Command("taskkill", "/F", "/IM", installedExeNam,
		"/FI", fmt.Sprintf("PID ne %d", os.Getpid()))).Run()
	time.Sleep(700 * time.Millisecond)

	if err := copyFile(self, target); err != nil {
		return false // fall back to running from where we are
	}
	_ = hidden(exec.Command("reg", "add", runKey, "/v", runValue, "/t", "REG_SZ",
		"/d", `"`+target+`"`, "/f")).Run()

	if err := exec.Command(target).Start(); err != nil {
		return false
	}
	return true
}

func doUninstall(dir string) {
	_ = hidden(exec.Command("reg", "delete", runKey, "/v", runValue, "/f")).Run()
	_ = hidden(exec.Command("taskkill", "/F", "/IM", installedExeNam,
		"/FI", fmt.Sprintf("PID ne %d", os.Getpid()))).Run()
	time.Sleep(500 * time.Millisecond)
	_ = os.RemoveAll(dir)
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	tmp := dst + ".new"
	out, err := os.Create(tmp)
	if err != nil {
		return err
	}
	if _, err := io.Copy(out, in); err != nil {
		out.Close()
		return err
	}
	if err := out.Close(); err != nil {
		return err
	}
	_ = os.Remove(dst)
	return os.Rename(tmp, dst)
}

// keepAwake stops the tablet from dimming or sleeping while BluRemote runs.
func keepAwake() {
	k32 := syscall.NewLazyDLL("kernel32.dll")
	set := k32.NewProc("SetThreadExecutionState")
	go func() {
		for {
			set.Call(uintptr(esSystemReq | esDisplayReq))
			time.Sleep(30 * time.Second)
		}
	}()
}

func findBrowser() (string, []string) {
	pf := os.Getenv("ProgramFiles")
	pf86 := os.Getenv("ProgramFiles(x86)")
	la := os.Getenv("LOCALAPPDATA")
	// A separate profile makes Edge/Chrome start a fresh full-screen window
	// even if the browser is already open in the background.
	profile := "--user-data-dir=" + filepath.Join(appDataDir(), "browser-profile")
	edge := []string{
		filepath.Join(pf, "Microsoft", "Edge", "Application", "msedge.exe"),
		filepath.Join(pf86, "Microsoft", "Edge", "Application", "msedge.exe"),
	}
	for _, p := range edge {
		if fileExists(p) {
			return p, []string{"--kiosk", "--edge-kiosk-type=fullscreen", "--no-first-run", profile}
		}
	}
	firefox := []string{
		filepath.Join(pf, "Mozilla Firefox", "firefox.exe"),
		filepath.Join(pf86, "Mozilla Firefox", "firefox.exe"),
		filepath.Join(la, "Mozilla Firefox", "firefox.exe"),
	}
	for _, p := range firefox {
		if fileExists(p) {
			return p, []string{"--kiosk"}
		}
	}
	chrome := []string{
		filepath.Join(pf, "Google", "Chrome", "Application", "chrome.exe"),
		filepath.Join(pf86, "Google", "Chrome", "Application", "chrome.exe"),
		filepath.Join(la, "Google", "Chrome", "Application", "chrome.exe"),
	}
	for _, p := range chrome {
		if fileExists(p) {
			return p, []string{"--kiosk", "--no-first-run", profile}
		}
	}
	return "", nil
}

func fileExists(p string) bool {
	if p == "" {
		return false
	}
	st, err := os.Stat(p)
	return err == nil && !st.IsDir()
}

func openBrowser(u string) {
	if exe, args := findBrowser(); exe != "" {
		cmd := exec.Command(exe, append(args, u)...)
		if err := cmd.Start(); err == nil {
			browserProc = cmd.Process
			return
		}
	}
	// Fallback: default browser (old Edge on this tablet).
	_ = hidden(exec.Command("rundll32", "url.dll,FileProtocolHandler", u)).Start()
}

func closeBrowser() {
	if browserProc != nil {
		_ = browserProc.Kill()
	}
}
