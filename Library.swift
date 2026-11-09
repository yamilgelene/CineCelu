import AppKit
import Darwin
import Foundation

struct Clip: Identifiable, Hashable {
    let id: String
    let name: String
    let path: String
    let aaePath: String?
    let bytes: Int64
    let modified: Date
    var selected: Bool
}

final class Library: ObservableObject {
    @Published var phoneName = ""
    @Published var clips: [Clip] = []
    @Published var status = "Buscando el iPhone…"
    @Published var busy = false
    @Published var progressLabel = ""
    @Published var fraction: Double = 0
    @Published var destination: String
    @Published var confirmDelete = false
    @Published var alertMessage: String?

    private let queue = DispatchQueue(label: "cinecelu.phone")
    private let defaultsKey = "destinationPath"
    private var pulse: UnsafeMutableRawPointer?
    private var pulseTimer: Timer?

    init() {
        let saved = UserDefaults.standard.string(forKey: defaultsKey) ?? ""
        destination = saved
    }

    var selectedClips: [Clip] { clips.filter(\.selected) }

    var selectedBytes: Int64 { selectedClips.reduce(0) { $0 + $1.bytes } }

    func rememberDestination() {
        UserDefaults.standard.set(destination, forKey: defaultsKey)
    }

    func refresh(keepStatus: String? = nil) {
        guard !busy else { return }
        busy = true
        if keepStatus == nil {
            status = "Leyendo la cámara…"
        }
        queue.async { [weak self] in
            var name = [CChar](repeating: 0, count: 256)
            var err = [CChar](repeating: 0, count: 512)
            var session: UnsafeMutableRawPointer?
            let opened = phone_open(&session, &name, name.count, &err, err.count)
            if opened != 0 || session == nil {
                let message = String(cString: err)
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.busy = false
                    self.phoneName = ""
                    self.clips = []
                    self.status = message.isEmpty ? "No hay un iPhone conectado." : message
                }
                return
            }
            var videos: UnsafeMutablePointer<PhoneVideo>?
            var count: Int32 = 0
            let listed = phone_list(session, &videos, &count, &err, err.count)
            var found: [Clip] = []
            if listed == 0, let videos {
                for index in 0..<Int(count) {
                    let video = videos[index]
                    let path = String(cString: video.path)
                    let aae = video.aae.map { String(cString: $0) }
                    found.append(Clip(
                        id: path,
                        name: String(cString: video.name),
                        path: path,
                        aaePath: (aae?.isEmpty == false) ? aae : nil,
                        bytes: video.bytes,
                        modified: Date(timeIntervalSince1970: TimeInterval(video.modified)),
                        selected: false
                    ))
                }
                phone_videos_free(videos, count)
            }
            let message = listed == 0 ? "" : String(cString: err)
            let device = String(cString: name)
            phone_close(session)
            found.sort { $0.modified > $1.modified }
            DispatchQueue.main.async {
                guard let self else { return }
                let previously = Set(self.clips.filter(\.selected).map(\.id))
                self.phoneName = device
                self.clips = found.map { clip in
                    var copy = clip
                    copy.selected = previously.contains(clip.id)
                    return copy
                }
                self.busy = false
                self.progressLabel = ""
                self.fraction = 0
                if let keepStatus {
                    self.status = keepStatus
                } else if listed != 0 {
                    self.status = message.isEmpty ? "No pude leer los videos." : message
                } else if found.isEmpty {
                    self.status = "\(device) no tiene videos en la cámara."
                } else {
                    self.status = "\(found.count) videos en \(device)."
                }
            }
        }
    }

    func selectToday() {
        let calendar = Calendar.current
        for index in clips.indices {
            clips[index].selected = calendar.isDateInToday(clips[index].modified)
        }
    }

    func selectNone() {
        for index in clips.indices { clips[index].selected = false }
    }

    func chooseDestination() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.prompt = "Elegir"
        panel.message = "Carpeta donde se guardan los videos."
        if !destination.isEmpty {
            panel.directoryURL = URL(fileURLWithPath: destination)
        }
        if panel.runModal() == .OK, let url = panel.url {
            destination = url.path
            rememberDestination()
        }
    }

    func copySelected() {
        let items = selectedClips
        guard !items.isEmpty else {
            alertMessage = "Elegí al menos un video."
            return
        }
        guard !destination.isEmpty else {
            alertMessage = "Elegí la carpeta de destino."
            return
        }
        let needed = items.reduce(Int64(0)) { partial, clip in
            partial + clip.bytes + 4096
        }
        if let free = freeBytes(at: destination), free < needed {
            alertMessage = "En esa carpeta no entra la selección. Hacen falta \(formatBytes(needed)) y hay \(formatBytes(free))."
            return
        }
        if destination.hasPrefix("/Users/") || destination.hasPrefix("/System/Volumes/Data") {
            status = "Ojo: esa carpeta está en el disco interno de la Mac."
        }
        busy = true
        fraction = 0
        progressLabel = "Preparando la copia…"
        let folder = destination
        startPulse()
        queue.async { [weak self] in
            self?.runCopy(items, folder: folder)
        }
    }

    func deleteSelected() {
        let items = selectedClips
        guard !items.isEmpty else {
            alertMessage = "Elegí los videos que ya copiaste."
            return
        }
        busy = true
        let folder = destination
        queue.async { [weak self] in
            self?.runDelete(items, folder: folder)
        }
    }

    private func runCopy(_ items: [Clip], folder: String) {
        var err = [CChar](repeating: 0, count: 512)
        var session: UnsafeMutableRawPointer?
        var name = [CChar](repeating: 0, count: 256)
        if phone_open(&session, &name, name.count, &err, err.count) != 0 || session == nil {
            finish(String(cString: err))
            return
        }
        defer { phone_close(session) }
        do {
            try FileManager.default.createDirectory(atPath: folder, withIntermediateDirectories: true)
        } catch {
            finish("No pude crear la carpeta de destino.")
            return
        }
        var copied = 0
        var failed: [String] = []
        for (index, clip) in items.enumerated() {
            let local = (folder as NSString).appendingPathComponent(clip.name)
            if FileManager.default.fileExists(atPath: local),
               fileSize(local) == clip.bytes {
                copied += 1
                clip.name.withCString { phone_pulse_set_file(pulse, $0, Int32(index), Int32(items.count), clip.bytes) }
                copySidecar(clip, folder: folder, session: session)
                continue
            }
            clip.name.withCString { phone_pulse_set_file(pulse, $0, Int32(index), Int32(items.count), clip.bytes) }
            var rc: Int32 = -1
            for attempt in 0..<4 {
                rc = clip.path.withCString { remote in
                    local.withCString { dest in
                        phone_copy(session, remote, dest, pulse, &err, err.count)
                    }
                }
                if rc == 0 || !Self.retryableCopyError(rc) || attempt == 3 { break }
                guard reopenPhone(&session) else {
                    finish("Se cortó la conexión con el iPhone. Quedaron copiados \(copied). Volvé a tocar Copiar: lo que ya está completo no se vuelve a bajar.")
                    return
                }
            }
            if rc != 0 {
                failed.append(clip.name)
                continue
            }
            try? FileManager.default.setAttributes([.modificationDate: clip.modified], ofItemAtPath: local)
            copySidecar(clip, folder: folder, session: session)
            copied += 1
        }
        if failed.isEmpty {
            finish("Copié \(copied) \(copied == 1 ? "video" : "videos") en \(folder).")
        } else {
            finish("Copié \(copied). No pude terminar \(failed.joined(separator: ", ")). Volvé a tocar Copiar.")
        }
    }

    private static func retryableCopyError(_ rc: Int32) -> Bool {
        [4, 11, 12, 20, 21].contains(rc)
    }

    private func reopenPhone(_ session: inout UnsafeMutableRawPointer?) -> Bool {
        if let session { phone_close(session) }
        session = nil
        Thread.sleep(forTimeInterval: 1)
        var err = [CChar](repeating: 0, count: 512)
        var name = [CChar](repeating: 0, count: 256)
        return phone_open(&session, &name, name.count, &err, err.count) == 0 && session != nil
    }

    private func copySidecar(_ clip: Clip, folder: String, session: UnsafeMutableRawPointer?) {
        guard let remote = clip.aaePath else { return }
        let filename = (remote as NSString).lastPathComponent
        let local = (folder as NSString).appendingPathComponent(filename)
        var ignored = [CChar](repeating: 0, count: 64)
        _ = remote.withCString { remotePath in
            local.withCString { dest in
                phone_copy(session, remotePath, dest, nil, &ignored, ignored.count)
            }
        }
    }

    private func runDelete(_ items: [Clip], folder: String) {
        var err = [CChar](repeating: 0, count: 512)
        var session: UnsafeMutableRawPointer?
        var name = [CChar](repeating: 0, count: 256)
        if phone_open(&session, &name, name.count, &err, err.count) != 0 || session == nil {
            finish(String(cString: err))
            return
        }
        defer { phone_close(session) }
        var removed = 0
        var skipped: [String] = []
        for clip in items {
            let local = (folder as NSString).appendingPathComponent(clip.name)
            let localSize = fileSize(local)
            var remoteSize: Int64 = -1
            let sizeRC = clip.path.withCString { phone_size(session, $0, &remoteSize, &err, err.count) }
            guard sizeRC == 0, localSize == remoteSize, localSize > 0 else {
                skipped.append(clip.name)
                continue
            }
            if clip.path.withCString({ phone_remove(session, $0, &err, err.count) }) != 0 {
                finish(String(cString: err))
                return
            }
            if let aae = clip.aaePath {
                let aaeName = (aae as NSString).lastPathComponent
                let localAAE = (folder as NSString).appendingPathComponent(aaeName)
                var aaeRemote: Int64 = -1
                if aae.withCString({ phone_size(session, $0, &aaeRemote, &err, err.count) }) == 0,
                   fileSize(localAAE) == aaeRemote {
                    _ = aae.withCString { phone_remove(session, $0, &err, err.count) }
                }
            }
            removed += 1
        }
        var message = "Borré \(removed) \(removed == 1 ? "video" : "videos") del iPhone."
        if !skipped.isEmpty {
            message += " Dejé \(skipped.count) porque la copia local no coincide."
        }
        message += " Si Fotos en iCloud los tenía, pueden volver a aparecer."
        finish(message)
    }

    private func startPulse() {
        pulseTimer?.invalidate()
        if pulse == nil {
            pulse = phone_pulse_new()
        }
        pulseTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            self?.pollPulse()
        }
    }

    private func pollPulse() {
        guard let pulse else { return }
        var name = [CChar](repeating: 0, count: 160)
        var done: Int64 = 0
        var total: Int64 = 0
        var index: Int32 = 0
        var count: Int32 = 0
        phone_pulse_read(pulse, &name, name.count, &done, &total, &index, &count)
        let label = String(cString: name)
        guard !label.isEmpty, count > 0 else { return }
        let fileFraction = total > 0 ? Double(done) / Double(total) : 0
        fraction = (Double(index) + fileFraction) / Double(count)
        progressLabel = "\(label)  \(formatBytes(done)) / \(formatBytes(total))"
    }

    private func finish(_ message: String) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.pulseTimer?.invalidate()
            self.pulseTimer = nil
            self.busy = false
            self.progressLabel = ""
            self.fraction = 0
            self.status = message
            self.refresh(keepStatus: message)
        }
    }

    private func fileSize(_ path: String) -> Int64 {
        let values = try? FileManager.default.attributesOfItem(atPath: path)
        return (values?[.size] as? NSNumber)?.int64Value ?? -1
    }

    private func freeBytes(at path: String) -> Int64? {
        var stats = statfs()
        let probe = FileManager.default.fileExists(atPath: path) ? path : (path as NSString).deletingLastPathComponent
        guard statfs(probe, &stats) == 0 else { return nil }
        return Int64(stats.f_bavail) * Int64(stats.f_bsize)
    }
}

func formatBytes(_ bytes: Int64) -> String {
    let value = Double(bytes)
    if value >= 1_000_000_000 { return String(format: "%.1f GB", value / 1_000_000_000) }
    if value >= 1_000_000 { return String(format: "%.0f MB", value / 1_000_000) }
    if value >= 1_000 { return String(format: "%.0f KB", value / 1_000) }
    return "\(bytes) B"
}
