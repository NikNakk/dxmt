// Inspect saved native-scale harness PNGs. The deliberately colored fixture
// must not contain white root background along the right/bottom content edge.
import Foundation
import CoreGraphics
import ImageIO

let args = CommandLine.arguments
if args.count != 2 { fputs("usage: edges <capture-directory>\n", stderr); exit(1) }
let directory = URL(fileURLWithPath: args[1])
let files = try FileManager.default.contentsOfDirectory(at:directory,includingPropertiesForKeys:nil)
  .filter { $0.pathExtension == "png" }.sorted { $0.lastPathComponent < $1.lastPathComponent }
if files.isEmpty { fputs("no PNG samples\n",stderr);exit(1) }
print("file,width,height,right_white,bottom_white,central_coverage,right_coverage,bottom_coverage")
for file in files {
  guard let source = CGImageSourceCreateWithURL(file as CFURL,nil), let image = CGImageSourceCreateImageAtIndex(source,0,nil) else { exit(1) }
  let width = image.width, height = image.height
  var pixels = [UInt8](repeating:0,count:width*height*4)
  let result:(Int,Int,Double,Double,Double) = pixels.withUnsafeMutableBytes { raw in
    let bitmap = CGContext(data:raw.baseAddress,width:width,height:height,bitsPerComponent:8,bytesPerRow:width*4,
      space:CGColorSpaceCreateDeviceRGB(),bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!
    bitmap.draw(image,in:CGRect(x:0,y:0,width:width,height:height))
    let bytes = raw.bindMemory(to:UInt8.self)
    func white(_ x:Int,_ y:Int) -> Bool {
      let i=(y*width+x)*4
      return bytes[i]>=250 && bytes[i+1]>=250 && bytes[i+2]>=250 && bytes[i+3]>=250
    }
    // Exclude titlebar/shadows/corners. Inspect the outermost device-pixel
    // column; the odd-dimension failure reported upstream was exactly one px.
    let right = (height/5..<height*4/5).filter { white(width-1,$0) }.count
    let bottom = (width/10..<width*9/10).filter { white($0,height-1) }.count
    var center=0.0,n=0.0, rightAlpha=0.0,bottomAlpha=0.0
    for y in height*2/5..<height*3/5 {
      for x in width*2/5..<width*3/5 { center += Double(bytes[(y*width+x)*4+3])/255; n += 1 }
    }
    for y in height/5..<height*4/5 { rightAlpha += Double(bytes[(y*width+width-1)*4+3])/255 }
    for x in width/10..<width*9/10 { bottomAlpha += Double(bytes[((height-1)*width+x)*4+3])/255 }
    return (right,bottom,center/n,rightAlpha/Double(height*4/5-height/5),bottomAlpha/Double(width*9/10-width/10))
  }
  print("\(file.lastPathComponent),\(width),\(height),\(result.0),\(result.1),\(result.2),\(result.3),\(result.4)")
}
