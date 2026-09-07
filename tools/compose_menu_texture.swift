import AppKit
import Foundation

if CommandLine.arguments.count == 3 {
    guard let source = NSImage(contentsOfFile: CommandLine.arguments[1]),
          let tiff = source.tiffRepresentation,
          let bitmap = NSBitmapImageRep(data: tiff),
          let png = bitmap.representation(using: .png, properties: [:]) else {
        fputs("could not convert input image\n", stderr)
        exit(1)
    }
    try png.write(to: URL(fileURLWithPath: CommandLine.arguments[2]))
    exit(0)
}

guard CommandLine.arguments.count == 4 else {
    fputs("usage: compose_menu_texture input.bmp output.png | left.png right.png output.png\n", stderr)
    exit(2)
}

let leftPath = CommandLine.arguments[1]
let rightPath = CommandLine.arguments[2]
let outputPath = CommandLine.arguments[3]

guard let left = NSImage(contentsOfFile: leftPath),
      let right = NSImage(contentsOfFile: rightPath) else {
    fputs("could not read input panel\n", stderr)
    exit(1)
}

let outputSize = NSSize(width: 2048, height: 512)
let output = NSImage(size: outputSize)
output.lockFocus()
NSGraphicsContext.current?.imageInterpolation = .high
left.draw(in: NSRect(x: 0, y: 0, width: 1024, height: 512))
right.draw(in: NSRect(x: 1024, y: 0, width: 1024, height: 512))
output.unlockFocus()

guard let tiff = output.tiffRepresentation,
      let bitmap = NSBitmapImageRep(data: tiff),
      let png = bitmap.representation(using: .png, properties: [:]) else {
    fputs("could not encode output PNG\n", stderr)
    exit(1)
}

try png.write(to: URL(fileURLWithPath: outputPath))
