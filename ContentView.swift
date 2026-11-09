import SwiftUI

struct ContentView: View {
    @StateObject private var library = Library()

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header
            destinationBar
            clipList
            footer
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .onAppear { library.refresh() }
        .alert("CineCelu", isPresented: alertShown) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(library.alertMessage ?? "")
        }
        .confirmationDialog(
            "¿Borrar del iPhone los videos elegidos?",
            isPresented: $library.confirmDelete,
            titleVisibility: .visible
        ) {
            Button("Borrar del iPhone", role: .destructive) { library.deleteSelected() }
            Button("Cancelar", role: .cancel) {}
        } message: {
            Text("Solo se borran los que ya están en la carpeta de destino con el mismo tamaño. Si estaban en iCloud, pueden volver a bajar.")
        }
    }

    private var alertShown: Binding<Bool> {
        Binding(
            get: { library.alertMessage != nil },
            set: { if !$0 { library.alertMessage = nil } }
        )
    }

    private var header: some View {
        HStack(alignment: .firstTextBaseline) {
            VStack(alignment: .leading, spacing: 4) {
                Text(library.phoneName.isEmpty ? "CineCelu" : library.phoneName)
                    .font(.title2.weight(.semibold))
                Text(library.status)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }
            Spacer()
            Button("Actualizar") { library.refresh() }
                .disabled(library.busy)
        }
        .padding(.horizontal, 20)
        .padding(.top, 18)
        .padding(.bottom, 12)
    }

    private var destinationBar: some View {
        HStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 2) {
                Text("Destino")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text(library.destination.isEmpty ? "Elegí una carpeta" : library.destination)
                    .font(.body.monospaced())
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
            Spacer()
            Button("Elegir carpeta…") { library.chooseDestination() }
                .disabled(library.busy)
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 10)
        .background(Color(nsColor: .controlBackgroundColor))
    }

    private var clipList: some View {
        List {
            ForEach($library.clips) { $clip in
                HStack(spacing: 12) {
                    Toggle("", isOn: $clip.selected)
                        .toggleStyle(.checkbox)
                        .labelsHidden()
                        .disabled(library.busy)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(clip.name)
                            .font(.body.weight(.medium))
                        Text(clip.modified.formatted(date: .abbreviated, time: .shortened))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    Spacer()
                    Text(formatBytes(clip.bytes))
                        .font(.body.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
                .padding(.vertical, 2)
            }
        }
        .listStyle(.inset)
    }

    private var footer: some View {
        VStack(alignment: .leading, spacing: 8) {
            if library.busy && !library.progressLabel.isEmpty {
                ProgressView(value: library.fraction)
                Text(library.progressLabel)
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
            HStack {
                Button("Hoy") { library.selectToday() }
                    .disabled(library.busy || library.clips.isEmpty)
                Button("Ninguno") { library.selectNone() }
                    .disabled(library.busy || library.clips.isEmpty)
                Text(selectionSummary)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                Spacer()
                Button("Borrar del teléfono") { library.confirmDelete = true }
                    .disabled(library.busy || library.selectedClips.isEmpty)
                Button("Copiar al disco") { library.copySelected() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(library.busy || library.selectedClips.isEmpty || library.destination.isEmpty)
            }
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 14)
    }

    private var selectionSummary: String {
        let count = library.selectedClips.count
        if count == 0 { return "Nada elegido" }
        return "\(count) \(count == 1 ? "video" : "videos") · \(formatBytes(library.selectedBytes))"
    }
}
