import AppKit

// Motor do 808 Reels Capture: app sem ícone no Dock, aberta pelo plugin.
// Fala com o plugin por socket local (127.0.0.1:48808).

if CommandLine.arguments.contains("--selftest") {
    exit(SelfTest.run())
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)

let engine = Engine()
engine.start()

app.run()
