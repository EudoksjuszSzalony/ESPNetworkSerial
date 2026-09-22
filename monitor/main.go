package main

import (
	"bufio"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	monitorName       = "ESPNetworkSerialMonitor"
	monitorVersion    = "0.2.0-dev"
	protocolVersion   = 1
	defaultDevicePort = "3233"
	dialTimeout       = 4 * time.Second
	reconnectGrace    = 15 * time.Second
	reconnectInterval = 250 * time.Millisecond
)

type response struct {
	EventType       string           `json:"eventType"`
	Message         string           `json:"message"`
	Error           bool             `json:"error,omitempty"`
	ProtocolVersion int              `json:"protocolVersion,omitempty"`
	PortDescription *portDescription `json:"port_description,omitempty"`
}

type portDescription struct {
	Protocol string `json:"protocol"`
}

type jsonOutput struct {
	mu  sync.Mutex
	enc *json.Encoder
}

func newJSONOutput(w io.Writer) *jsonOutput {
	enc := json.NewEncoder(w)
	enc.SetEscapeHTML(false)
	return &jsonOutput{enc: enc}
}

func (o *jsonOutput) send(r response) {
	o.mu.Lock()
	defer o.mu.Unlock()
	_ = o.enc.Encode(r)
}

func (o *jsonOutput) ok(event string) {
	o.send(response{EventType: event, Message: "OK"})
}

func (o *jsonOutput) fail(event, message string) {
	o.send(response{EventType: event, Message: message, Error: true})
}

type reconnectingTCP struct {
	address           string
	dialTimeout       time.Duration
	reconnectGrace    time.Duration
	reconnectInterval time.Duration

	stateMu sync.Mutex
	conn    net.Conn
	closed  bool

	reconnectMu sync.Mutex
}

func newReconnectingTCP(address string, initial net.Conn, grace time.Duration) *reconnectingTCP {
	return &reconnectingTCP{
		address:           address,
		dialTimeout:       dialTimeout,
		reconnectGrace:    grace,
		reconnectInterval: reconnectInterval,
		conn:              initial,
	}
}

func (r *reconnectingTCP) current() (net.Conn, bool) {
	r.stateMu.Lock()
	defer r.stateMu.Unlock()

	if r.closed {
		return nil, false
	}
	return r.conn, r.conn != nil
}

func (r *reconnectingTCP) isClosed() bool {
	r.stateMu.Lock()
	defer r.stateMu.Unlock()
	return r.closed
}

func (r *reconnectingTCP) invalidate(conn net.Conn) {
	if conn == nil {
		return
	}

	r.stateMu.Lock()
	if r.conn == conn {
		r.conn = nil
	}
	r.stateMu.Unlock()

	_ = conn.Close()
}

func (r *reconnectingTCP) reconnect(deadline time.Time) (net.Conn, error) {
	r.reconnectMu.Lock()
	defer r.reconnectMu.Unlock()

	if conn, ok := r.current(); ok {
		return conn, nil
	}
	if r.isClosed() {
		return nil, net.ErrClosed
	}

	fmt.Fprintf(os.Stderr, "%s: connection to %s lost; waiting up to %s for reconnect\n",
		monitorName, r.address, time.Until(deadline).Round(time.Millisecond))

	for {
		if r.isClosed() {
			return nil, net.ErrClosed
		}

		remaining := time.Until(deadline)
		if remaining <= 0 {
			return nil, fmt.Errorf("reconnect timeout after %s", r.reconnectGrace)
		}

		timeout := r.dialTimeout
		if remaining < timeout {
			timeout = remaining
		}

		conn, err := dialESPNS(r.address, timeout)
		if err == nil {
			r.stateMu.Lock()
			if r.closed {
				r.stateMu.Unlock()
				_ = conn.Close()
				return nil, net.ErrClosed
			}

			if r.conn == nil {
				r.conn = conn
				r.stateMu.Unlock()
				fmt.Fprintf(os.Stderr, "%s: reconnected to %s\n", monitorName, r.address)
				return conn, nil
			}

			existing := r.conn
			r.stateMu.Unlock()
			_ = conn.Close()
			return existing, nil
		}

		sleepFor := r.reconnectInterval
		if remaining < sleepFor {
			sleepFor = remaining
		}
		if sleepFor > 0 {
			time.Sleep(sleepFor)
		}
	}
}

func (r *reconnectingTCP) connection(deadline *time.Time) (net.Conn, error) {
	if conn, ok := r.current(); ok {
		return conn, nil
	}

	if deadline.IsZero() {
		*deadline = time.Now().Add(r.reconnectGrace)
	}

	return r.reconnect(*deadline)
}

func (r *reconnectingTCP) Read(p []byte) (int, error) {
	var deadline time.Time

	for {
		conn, err := r.connection(&deadline)
		if err != nil {
			return 0, err
		}

		n, readErr := conn.Read(p)
		if readErr == nil {
			return n, nil
		}

		r.invalidate(conn)

		if n > 0 {
			return n, nil
		}
	}
}

func (r *reconnectingTCP) Write(p []byte) (int, error) {
	total := 0
	var deadline time.Time

	for total < len(p) {
		conn, err := r.connection(&deadline)
		if err != nil {
			return total, err
		}

		n, writeErr := conn.Write(p[total:])
		total += n

		if writeErr == nil {
			if n == 0 {
				return total, io.ErrShortWrite
			}
			continue
		}

		r.invalidate(conn)
	}

	return total, nil
}

func (r *reconnectingTCP) Close() error {
	r.stateMu.Lock()
	if r.closed {
		r.stateMu.Unlock()
		return nil
	}

	r.closed = true
	conn := r.conn
	r.conn = nil
	r.stateMu.Unlock()

	if conn != nil {
		return conn.Close()
	}
	return nil
}

type session struct {
	board  *reconnectingTCP
	client net.Conn
	once   sync.Once
}

type app struct {
	out *jsonOutput

	mu          sync.Mutex
	initialized bool
	active      *session
}

func newApp(out io.Writer) *app {
	return &app{out: newJSONOutput(out)}
}

func (a *app) run(in io.Reader) error {
	scanner := bufio.NewScanner(in)
	scanner.Buffer(make([]byte, 1024), 64*1024)

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		quit := a.handleCommand(line)
		if quit {
			a.closeSilently()
			return nil
		}
	}

	a.closeSilently()
	return scanner.Err()
}

func (a *app) handleCommand(line string) bool {
	command, rest := splitCommand(line)

	if command != "HELLO" && command != "QUIT" && !a.isInitialized() {
		a.out.fail("command_error", fmt.Sprintf("First command must be HELLO, but got '%s'", command))
		return false
	}

	switch command {
	case "HELLO":
		a.hello(rest)
	case "DESCRIBE":
		a.describe(rest)
	case "CONFIGURE":
		a.configure(rest)
	case "OPEN":
		a.open(rest)
	case "CLOSE":
		a.closeCommand(rest)
	case "QUIT":
		if strings.TrimSpace(rest) != "" {
			a.out.fail("quit", "QUIT does not accept parameters")
			return false
		}
		a.out.ok("quit")
		return true
	default:
		a.out.fail("command_error", fmt.Sprintf("Command %s not supported", command))
	}

	return false
}

func splitCommand(line string) (string, string) {
	if i := strings.IndexByte(line, ' '); i >= 0 {
		return strings.ToUpper(line[:i]), strings.TrimSpace(line[i+1:])
	}
	return strings.ToUpper(line), ""
}

var helloPattern = regexp.MustCompile(`^(\d+)\s+"([^"]+)"$`)

func (a *app) hello(rest string) {
	if a.isInitialized() {
		a.out.fail("hello", "HELLO already called")
		return
	}

	match := helloPattern.FindStringSubmatch(rest)
	if len(match) != 3 {
		a.out.fail("hello", "Invalid HELLO command")
		return
	}

	requested, err := strconv.Atoi(match[1])
	if err != nil || requested < protocolVersion {
		a.out.fail("hello", fmt.Sprintf("Unsupported protocol version: %s", match[1]))
		return
	}

	a.mu.Lock()
	a.initialized = true
	a.mu.Unlock()

	a.out.send(response{
		EventType:       "hello",
		Message:         "OK",
		ProtocolVersion: protocolVersion,
	})
}

func (a *app) describe(rest string) {
	if rest != "" {
		a.out.fail("describe", "DESCRIBE does not accept parameters")
		return
	}

	a.out.send(response{
		EventType: "describe",
		Message:   "OK",
		PortDescription: &portDescription{
			Protocol: "network",
		},
	})
}

func (a *app) configure(rest string) {
	if strings.TrimSpace(rest) == "" {
		a.out.fail("configure", "Invalid CONFIGURE command")
		return
	}
	a.out.fail("configure", "ESPNetworkSerial network transport has no configurable monitor parameters")
}

func (a *app) open(rest string) {
	parameters := strings.SplitN(strings.TrimSpace(rest), " ", 2)
	if len(parameters) != 2 {
		a.out.fail("open", "Invalid OPEN command")
		return
	}

	clientAddress := strings.TrimSpace(parameters[0])
	boardPort := strings.Trim(strings.TrimSpace(parameters[1]), `"`)
	if clientAddress == "" || boardPort == "" {
		a.out.fail("open", "Invalid OPEN command")
		return
	}

	a.mu.Lock()
	if a.active != nil {
		a.mu.Unlock()
		a.out.fail("open", "port already opened")
		return
	}
	a.mu.Unlock()

	boardAddress, err := normalizeBoardAddress(boardPort)
	if err != nil {
		a.out.fail("open", err.Error())
		return
	}

	initialBoardConn, err := dialESPNS(boardAddress, dialTimeout)
	if err != nil {
		a.out.fail("open", fmt.Sprintf("could not connect to ESP32 at %s: %v", boardAddress, err))
		return
	}

	clientConn, err := net.DialTimeout("tcp", clientAddress, dialTimeout)
	if err != nil {
		_ = initialBoardConn.Close()
		a.out.fail("open", fmt.Sprintf("could not connect back to Arduino IDE at %s: %v", clientAddress, err))
		return
	}

	boardConn := newReconnectingTCP(boardAddress, initialBoardConn, reconnectGrace)
	s := &session{board: boardConn, client: clientConn}

	a.mu.Lock()
	if a.active != nil {
		a.mu.Unlock()
		_ = boardConn.Close()
		_ = clientConn.Close()
		a.out.fail("open", "port already opened")
		return
	}
	a.active = s
	a.mu.Unlock()

	a.out.ok("open")

	go a.bridge(s, s.client, s.board, "ESP32 connection unavailable")
	go a.bridge(s, s.board, s.client, "Arduino IDE connection closed")
}

func (a *app) bridge(s *session, dst io.Writer, src io.Reader, label string) {
	_, err := io.Copy(dst, src)
	message := label
	if err != nil && !errors.Is(err, net.ErrClosed) {
		message += ": " + err.Error()
	}
	a.endSession(s, "port_closed", message, true)
}

func (a *app) closeCommand(rest string) {
	if strings.TrimSpace(rest) != "" {
		a.out.fail("close", "CLOSE does not accept parameters")
		return
	}

	a.mu.Lock()
	s := a.active
	a.mu.Unlock()
	if s == nil {
		a.out.fail("close", "port already closed")
		return
	}

	if !a.endSession(s, "close", "OK", false) {
		a.out.fail("close", "port already closed")
	}
}

func (a *app) closeSilently() {
	a.mu.Lock()
	s := a.active
	a.mu.Unlock()
	if s != nil {
		a.endSession(s, "", "", false)
	}
}

func (a *app) endSession(s *session, eventType, message string, isError bool) bool {
	ended := false
	s.once.Do(func() {
		ended = true
		_ = s.board.Close()
		_ = s.client.Close()

		a.mu.Lock()
		if a.active == s {
			a.active = nil
		}
		a.mu.Unlock()

		if eventType != "" {
			a.out.send(response{EventType: eventType, Message: message, Error: isError})
		}
	})
	return ended
}

func (a *app) isInitialized() bool {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.initialized
}

func normalizeBoardAddress(boardPort string) (string, error) {
	boardPort = strings.TrimSpace(strings.Trim(boardPort, `"`))
	if boardPort == "" {
		return "", errors.New("empty board port")
	}

	if _, _, err := net.SplitHostPort(boardPort); err == nil {
		return boardPort, nil
	}

	if ip := net.ParseIP(strings.Split(boardPort, "%")[0]); ip != nil && strings.Contains(boardPort, ":") {
		return net.JoinHostPort(boardPort, defaultDevicePort), nil
	}

	if strings.Contains(boardPort, ":") {
		return "", fmt.Errorf("invalid board address %q", boardPort)
	}

	return net.JoinHostPort(boardPort, defaultDevicePort), nil
}

func directMode(target string) error {
	address, err := normalizeBoardAddress(target)
	if err != nil {
		return err
	}

	initialConn, err := dialESPNS(address, dialTimeout)
	if err != nil {
		return fmt.Errorf("connect to %s: %w", address, err)
	}

	conn := newReconnectingTCP(address, initialConn, reconnectGrace)
	defer conn.Close()

	fmt.Fprintf(os.Stderr, "%s %s connected to %s\n", monitorName, monitorVersion, address)
	fmt.Fprintf(os.Stderr, "Direct test mode: stdin/stdout are bridged to the ESP32; reconnect grace is %s. Press Ctrl+C to stop.\n", reconnectGrace)

	errCh := make(chan error, 2)
	go func() {
		_, copyErr := io.Copy(os.Stdout, conn)
		errCh <- copyErr
	}()
	go func() {
		_, copyErr := io.Copy(conn, os.Stdin)
		errCh <- copyErr
	}()

	err = <-errCh
	if err != nil && !errors.Is(err, net.ErrClosed) {
		return err
	}
	return nil
}

func main() {
	connect := flag.String("connect", "", "directly connect to an ESP32 address for transport testing (host or host:port)")
	showVersion := flag.Bool("version", false, "print version and exit")
	flag.Parse()

	if *showVersion {
		fmt.Printf("%s %s\n", monitorName, monitorVersion)
		return
	}

	if *connect != "" {
		if err := directMode(*connect); err != nil {
			fmt.Fprintln(os.Stderr, "error:", err)
			os.Exit(1)
		}
		return
	}

	application := newApp(os.Stdout)
	if err := application.run(os.Stdin); err != nil {
		fmt.Fprintln(os.Stderr, "monitor error:", err)
		os.Exit(1)
	}
}
