import AppKit
let out = CommandLine.arguments[1]
for (i, symbol) in ["🍎", "🍌", "🐱", "🐶", "🌙", "🌌"].enumerated() {
    let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 96, pixelsHigh: 96, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 384, bitsPerPixel: 32)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
    NSColor.clear.setFill(); NSRect(x: 0, y: 0, width: 96, height: 96).fill()
    let text = NSAttributedString(string: symbol, attributes: [.font: NSFont(name: "AppleColorEmoji", size: 82)!])
    let size = text.size()
    text.draw(at: NSPoint(x: (96-size.width)/2, y: (96-size.height)/2))
    NSGraphicsContext.restoreGraphicsState()
    try bitmap.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: "\(out)/art\(i).png"))
}
