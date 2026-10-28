// Independent check of the INSTALLED PowerComp AU through Apple's own host (AVAudioEngine, offline rendering),
// the same Audio Unit path Logic uses: a 100 Hz tone, 1.5 s at -6 dBFS then 1.5 s at -26 dBFS, through the plug-in
// with its default sound (Gentle Glue). Prints the loud/quiet gap in and out, the peak, and whether it is finite.
//   swift Tools/au_check.swift          exit code 0 when the gap shrank and the output is finite and below 0 dBFS
import AVFoundation

let desc = AudioComponentDescription(componentType: kAudioUnitType_Effect,
                                     componentSubType: 0x50776370 /* Pwcp */,
                                     componentManufacturer: 0x4d646479 /* Mddy */,
                                     componentFlags: 0, componentFlagsMask: 0)
let sr = 48000.0
let format = AVAudioFormat(standardFormatWithSampleRate: sr, channels: 2)!
var unit: AVAudioUnit?
let done = DispatchSemaphore(value: 0)
AVAudioUnit.instantiate(with: desc, options: []) { u, error in
    if let error = error { print("FAIL: cannot load PowerComp: \(error)") }
    unit = u
    done.signal()
}
done.wait()
guard let fx = unit else { exit(1) }
print("loaded: \(fx.name) by \(fx.manufacturerName), latency \(fx.auAudioUnit.latency) s")

let engine = AVAudioEngine()
let player = AVAudioPlayerNode()
engine.attach(player)
engine.attach(fx)
engine.connect(player, to: fx, format: format)
engine.connect(fx, to: engine.mainMixerNode, format: format)
try! engine.enableManualRenderingMode(.offline, format: format, maximumFrameCount: 512)
try! engine.start()

let total = AVAudioFrameCount(3.0 * sr)
let input = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: total)!
input.frameLength = total
for i in 0..<Int(total) {
    let a: Float = i < Int(1.5 * sr) ? 0.5012 : 0.0501 // -6 / -26 dBFS
    let x = a * Float(sin(2.0 * Double.pi * 100.0 * Double(i) / sr))
    input.floatChannelData![0][i] = x
    input.floatChannelData![1][i] = x
}
player.scheduleBuffer(input, at: nil)
player.play()

let out = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: total)!
let chunk = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat, frameCapacity: 512)!
while out.frameLength < total {
    let n = min(AVAudioFrameCount(512), total - out.frameLength)
    let status = try! engine.renderOffline(n, to: chunk)
    guard status == .success else { print("FAIL: render status \(status.rawValue)"); exit(1) }
    for c in 0..<2 {
        memcpy(out.floatChannelData![c] + Int(out.frameLength), chunk.floatChannelData![c], Int(chunk.frameLength) * 4)
    }
    out.frameLength += chunk.frameLength
}

func rms(_ b: AVAudioPCMBuffer, _ from: Double, _ to: Double) -> Double {
    var s = 0.0
    let a = Int(from * sr), z = Int(to * sr)
    for i in a..<z { let v = Double(b.floatChannelData![0][i]); s += v * v }
    return sqrt(s / Double(z - a))
}
func db(_ g: Double) -> Double { 20.0 * log10(max(g, 1e-12)) }
var peak: Float = 0
var finite = true
for c in 0..<2 { for i in 0..<Int(out.frameLength) { let v = out.floatChannelData![c][i]; finite = finite && v.isFinite; peak = max(peak, abs(v)) } }
let gapIn = db(rms(input, 1.2, 1.5) / rms(input, 2.7, 3.0))
let gapOut = db(rms(out, 1.2, 1.5) / rms(out, 2.7, 3.0))
print(String(format: "loud/quiet gap: in %.2f dB, out %.2f dB; output peak %.2f dBFS; finite %@", gapIn, gapOut, db(Double(peak)), finite ? "yes" : "NO"))
let ok = finite && gapOut < gapIn - 1.5 && peak < 1.0
print(ok ? "PASS" : "FAIL")
exit(ok ? 0 : 1)
