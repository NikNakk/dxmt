// Capture actual compositor output during churn; return codes are not pixels.
// Requires macOS 14+ and Screen Recording permission for the invoking terminal.
import Foundation
import AppKit
import ScreenCaptureKit
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

@main struct Capture {
  @MainActor static func main() async {
    _ = NSApplication.shared
    NSApp.setActivationPolicy(.prohibited)
    let args = CommandLine.arguments
    let title = args.count > 1 ? args[1] : "DXMT remote regression"
    let destination = args.count > 2 ? args[2] : "/tmp/dxmt-presentation-captures"
    let count = args.count > 3 ? Int(args[3]) ?? 40 : 40
    do {
      try FileManager.default.createDirectory(atPath: destination, withIntermediateDirectories: true)
      print("sample,time_ns,width,height,mean_r,mean_g,mean_b,near_black,central_coverage,geometry_changed,valid_sample")
      let deadline = DispatchTime.now().uptimeNanoseconds + 20_000_000_000
      while true {
        let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
        if content.windows.contains(where: { ($0.title ?? "").contains(title) }) { break }
        if DispatchTime.now().uptimeNanoseconds >= deadline {
          throw NSError(domain:"capture",code:1,userInfo:[NSLocalizedDescriptionKey:"Timed out waiting for matching window"])
        }
        try await Task.sleep(nanoseconds:200_000_000)
      }
      try await Task.sleep(nanoseconds:2_000_000_000)
      let start = DispatchTime.now().uptimeNanoseconds
      for sample in 0..<count {
        // Refresh the window snapshot so every sample uses the current size.
        let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
        guard let window = content.windows.first(where: { ($0.title ?? "").contains(title) }) else {
          throw NSError(domain: "capture", code: 1, userInfo: [NSLocalizedDescriptionKey: "No matching window: \(title)"])
        }
        let filter = SCContentFilter(desktopIndependentWindow: window)
        let configuration = SCStreamConfiguration()
        configuration.width = Int(window.frame.width * 2)
        configuration.height = Int(window.frame.height * 2)
        configuration.showsCursor = false
        let image = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: configuration)
        let url = URL(fileURLWithPath: destination).appendingPathComponent(String(format:"frame-%04d.png", sample))
        guard let output = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
          throw NSError(domain:"capture",code:2)
        }
        CGImageDestinationAddImage(output, image, nil)
        guard CGImageDestinationFinalize(output) else { throw NSError(domain:"capture",code:3) }
        let width = image.width, height = image.height
        var pixels = [UInt8](repeating:0,count:width*height*4)
        let stats: (Double,Double,Double,Double) = pixels.withUnsafeMutableBytes { bytes in
          let context = CGContext(data: bytes.baseAddress, width: width, height: height, bitsPerComponent: 8,
              bytesPerRow:width*4, space:CGColorSpaceCreateDeviceRGB(), bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!
          context.draw(image, in:CGRect(x:0,y:0,width:width,height:height))
          let data = bytes.bindMemory(to:UInt8.self)
          var r=0.0,g=0.0,b=0.0,n=0.0,alpha=0.0
          // Central patch avoids title bars and shadows, and stays within the
          // always-visible back surface even when the foreground child hides.
          for y in (height*2/5)..<(height*3/5) {
            for x in (width*2/5)..<(width*3/5) {
              let i=(y*width+x)*4
              r += Double(data[i]); g += Double(data[i+1]); b += Double(data[i+2]); alpha += Double(data[i+3])/255.0; n += 1
            }
          }
          return (r/n,g/n,b/n,alpha/n)
        }
        let post = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
        let current = post.windows.first(where: { $0.windowID == window.windowID })
        let geometryChanged = current?.frame != window.frame
        let valid = stats.3 >= 0.95 && !geometryChanged
        let black = max(stats.0, max(stats.1, stats.2)) < 8
        print("\(sample),\(DispatchTime.now().uptimeNanoseconds-start),\(width),\(height),\(stats.0),\(stats.1),\(stats.2),\(black),\(stats.3),\(geometryChanged),\(valid)")
        try await Task.sleep(nanoseconds:100_000_000)
      }
    } catch {
      FileHandle.standardError.write(Data("capture failed: \(error)\n".utf8))
      exit(1)
    }
  }
}
