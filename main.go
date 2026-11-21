// BluRemote — a tiny local helper that turns an old Windows tablet into a
// touch controller for BluOS / Bluesound players.
//
// It serves the controller page on http://127.0.0.1:11011 and forwards
// commands to the player's local API (port 11000). On Windows it also
// installs itself to start with Windows, keeps the screen awake and opens
// the page full-screen.
package main

import (
	"context"
	"embed"
	"encoding/json"
	"encoding/xml"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

//go:embed web
var webFS embed.FS

const (
	listenAddr = "127.0.0.1:11011"
	appURL     = "http://127.0.0.1:11011/"
	bluosPort  = "11000"
)

var version = "dev"

// ---------- config ----------

type Config struct {
	Player string `json:"player"` // host or ip of selected player
}

var (
	cfgMu   sync.Mutex
	cfg     Config
	cfgPath string
)

func loadConfig() {
	b, err := os.ReadFile(cfgPath)
	if err == nil {
		_ = json.Unmarshal(b, &cfg)
	}
}

func saveConfig() error {
	b, _ := json.MarshalIndent(cfg, "", "  ")
	_ = os.MkdirAll(filepath.Dir(cfgPath), 0o755)
	return os.WriteFile(cfgPath, b, 0o644)
}

func currentPlayer() string {
	cfgMu.Lock()
	defer cfgMu.Unlock()
	return cfg.Player
}

// ---------- discovery ----------

type Player struct {
	Host  string `json:"host"`
	Name  string `json:"name"`
	Model string `json:"model"`
}

type syncStatus struct {
	Name      string `xml:"name,attr"`
	ModelName string `xml:"modelName,attr"`
	Model     string `xml:"model,attr"`
}

var (
	scanMu      sync.Mutex
	lastScan    []Player
	lastScanAt  time.Time
	probeClient = &http.Client{Timeout: 900 * time.Millisecond}
)

func probe(host string) (Player, bool) {
	resp, err := probeClient.Get("http://" + net.JoinHostPort(host, bluosPort) + "/SyncStatus")
	if err != nil {
		return Player{}, false
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return Player{}, false
	}
	var s syncStatus
	if err := xml.NewDecoder(io.LimitReader(resp.Body, 64<<10)).Decode(&s); err != nil {
		return Player{}, false
	}
	model := s.ModelName
	if model == "" {
		model = s.Model
	}
	name := s.Name
	if name == "" {
		name = host
	}
	return Player{Host: host, Name: name, Model: model}, true
}

// localSubnets returns the /24 prefixes ("192.168.1.") of private IPv4 interfaces.
func localSubnets() []string {
	seen := map[string]bool{}
	var out []string
	ifaces, _ := net.Interfaces()
	for _, ifc := range ifaces {
		if ifc.Flags&net.FlagUp == 0 || ifc.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, _ := ifc.Addrs()
		for _, a := range addrs {
			ipn, ok := a.(*net.IPNet)
			if !ok {
				continue
			}
			ip := ipn.IP.To4()
			if ip == nil || !ip.IsPrivate() {
				continue
			}
			p := fmt.Sprintf("%d.%d.%d.", ip[0], ip[1], ip[2])
			if !seen[p] {
				seen[p] = true
				out = append(out, p)
			}
		}
	}
	return out
}

func scan(extra []string) []Player {
	var hosts []string
	for _, p := range localSubnets() {
		for i := 1; i < 255; i++ {
			hosts = append(hosts, fmt.Sprintf("%s%d", p, i))
		}
	}
	hosts = append(hosts, extra...)

	var (
		mu    sync.Mutex
		found = map[string]Player{}
		wg    sync.WaitGroup
		sem   = make(chan struct{}, 48) // gentle on a 1 GB tablet
	)
	for _, h := range hosts {
		wg.Add(1)
		sem <- struct{}{}
		go func(h string) {
			defer wg.Done()
			defer func() { <-sem }()
			if p, ok := probe(h); ok {
				mu.Lock()
				found[p.Host] = p
				mu.Unlock()
			}
		}(h)
	}
	wg.Wait()
	out := make([]Player, 0, len(found))
	for _, p := range found {
		out = append(out, p)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out
}

// ---------- http handlers ----------

var proxyClient = &http.Client{Timeout: 130 * time.Second} // allows BluOS long-poll

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	_ = json.NewEncoder(w).Encode(v)
}

func handlePlayers(w http.ResponseWriter, r *http.Request) {
	scanMu.Lock()
	defer scanMu.Unlock()
	if r.URL.Query().Get("rescan") == "1" || lastScan == nil || time.Since(lastScanAt) > 10*time.Minute {
		var extra []string
		if p := currentPlayer(); p != "" {
			extra = append(extra, p)
		}
		lastScan = scan(extra)
		lastScanAt = time.Now()
		if len(lastScan) == 0 {
			lastScanAt = time.Time{} // don't cache an empty result; try again next time
		}
	}
	writeJSON(w, map[string]any{"players": lastScan, "selected": currentPlayer(), "version": version})
}

func handleConfig(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodPost {
		var c Config
		if err := json.NewDecoder(io.LimitReader(r.Body, 4096)).Decode(&c); err != nil {
			http.Error(w, "bad json", 400)
			return
		}
		c.Player = strings.TrimSpace(c.Player)
		if c.Player != "" {
			p, ok := probe(c.Player)
			if !ok {
				http.Error(w, "No BluOS player answered at "+c.Player, 404)
				return
			}
			// Make sure a manually entered player shows up by name in the list.
			scanMu.Lock()
			known := false
			for _, x := range lastScan {
				if x.Host == p.Host {
					known = true
				}
			}
			if !known {
				lastScan = append(lastScan, p)
				if lastScanAt.IsZero() {
					lastScanAt = time.Now()
				}
			}
			scanMu.Unlock()
		}
		cfgMu.Lock()
		cfg = c
		err := saveConfig()
		cfgMu.Unlock()
		if err != nil {
			http.Error(w, err.Error(), 500)
			return
		}
	}
	cfgMu.Lock()
	defer cfgMu.Unlock()
	writeJSON(w, cfg)
}

// /bluos/<Endpoint>?query → http://player:11000/<Endpoint>?query
func handleBluOS(w http.ResponseWriter, r *http.Request) {
	host := r.URL.Query().Get("host")
	if host == "" {
		host = currentPlayer()
	}
	if host == "" {
		http.Error(w, "no player selected", 409)
		return
	}
	endpoint := strings.TrimPrefix(r.URL.Path, "/bluos/")
	if endpoint == "" || strings.Contains(endpoint, "..") {
		http.Error(w, "bad endpoint", 400)
		return
	}
	q := r.URL.Query()
	q.Del("host")
	target := "http://" + net.JoinHostPort(host, bluosPort) + "/" + endpoint
	if enc := q.Encode(); enc != "" {
		target += "?" + enc
	}
	forward(w, r.Context(), target)
}

// /art?u=<url or player-relative path>
func handleArt(w http.ResponseWriter, r *http.Request) {
	u := r.URL.Query().Get("u")
	if u == "" {
		http.NotFound(w, r)
		return
	}
	var target string
	if strings.HasPrefix(u, "http://") || strings.HasPrefix(u, "https://") {
		pu, err := url.Parse(u)
		if err != nil {
			http.Error(w, "bad url", 400)
			return
		}
		target = pu.String()
	} else {
		host := currentPlayer()
		if host == "" {
			http.NotFound(w, r)
			return
		}
		if !strings.HasPrefix(u, "/") {
			u = "/" + u
		}
		target = "http://" + net.JoinHostPort(host, bluosPort) + u
	}
	w.Header().Set("Cache-Control", "max-age=3600")
	forward(w, r.Context(), target)
}

func forward(w http.ResponseWriter, ctx context.Context, target string) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, target, nil)
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}
	resp, err := proxyClient.Do(req)
	if err != nil {
		if errors.Is(ctx.Err(), context.Canceled) {
			return
		}
		http.Error(w, "player unreachable: "+err.Error(), 502)
		return
	}
	defer resp.Body.Close()
	if ct := resp.Header.Get("Content-Type"); ct != "" {
		w.Header().Set("Content-Type", ct)
	}
	if w.Header().Get("Cache-Control") == "" {
		w.Header().Set("Cache-Control", "no-store")
	}
	w.WriteHeader(resp.StatusCode)
	_, _ = io.Copy(w, resp.Body)
}

func handleQuit(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", 405)
		return
	}
	writeJSON(w, map[string]bool{"ok": true})
	go func() {
		time.Sleep(300 * time.Millisecond)
		closeBrowser()
		os.Exit(0)
	}()
}

func main() {
	noBrowser := flag.Bool("no-browser", false, "do not open the controller page")
	uninstall := flag.Bool("uninstall", false, "remove autostart and installed copy")
	flag.Parse()

	dataDir := appDataDir()
	cfgPath = filepath.Join(dataDir, "config.json")

	if *uninstall {
		doUninstall(dataDir)
		return
	}

	// First run from Downloads: install to app folder, register autostart, relaunch.
	if relaunched := ensureInstalled(dataDir); relaunched {
		return
	}

	loadConfig()

	ln, err := net.Listen("tcp", listenAddr)
	if err != nil {
		// Already running: just bring the page up.
		if !*noBrowser {
			openBrowser(appURL)
		}
		return
	}

	sub, _ := fs.Sub(webFS, "web")
	mux := http.NewServeMux()
	mux.Handle("/", http.FileServer(http.FS(sub)))
	mux.HandleFunc("/api/players", handlePlayers)
	mux.HandleFunc("/api/config", handleConfig)
	mux.HandleFunc("/api/quit", handleQuit)
	mux.HandleFunc("/bluos/", handleBluOS)
	mux.HandleFunc("/art", handleArt)

	keepAwake()
	if !*noBrowser {
		go func() {
			time.Sleep(400 * time.Millisecond)
			openBrowser(appURL)
		}()
	}
	log.Printf("BluRemote %s on %s", version, appURL)
	log.Fatal(http.Serve(ln, mux))
}
