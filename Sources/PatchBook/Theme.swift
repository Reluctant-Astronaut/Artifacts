import SwiftUI

extension Color {
    init(hex: UInt32) {
        self.init(red: Double((hex >> 16) & 0xFF) / 255,
                  green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255)
    }
}

enum Theme {
    static let bg = Color(hex: 0x111312)
    static let panel = Color(hex: 0x1A1D1B)
    static let panel2 = Color(hex: 0x222623)
    static let well = Color(hex: 0x0D0F0E)
    static let line = Color(hex: 0x2F3530)
    static let line2 = Color(hex: 0x3A413B)
    static let text = Color(hex: 0xE8E5DC)
    static let mute = Color(hex: 0x8E958D)
    static let dim = Color(hex: 0x626961)
    static let green = Color(hex: 0x7D9A6F)
    static let greenDark = Color(hex: 0x3F5238)
    static let brass = Color(hex: 0xD1B065)
    static let red = Color(hex: 0xEF5350)
    static let amber = Color(hex: 0xF2A33A)
    static let blue = Color(hex: 0x58A6FF)
    static let white = Color(hex: 0xEEF3EA)
    static let violet = Color(hex: 0xB48CF5)
    static let ok = Color(hex: 0x86C77A)
    static let ledOff = Color(hex: 0x2C322D)
}

extension View {
    func card(padding: CGFloat = 10, dashed: Bool = false) -> some View {
        self
            .padding(padding)
            .background(RoundedRectangle(cornerRadius: 10).fill(Theme.panel))
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(Theme.line, style: StrokeStyle(lineWidth: 1, dash: dashed ? [5, 4] : []))
            )
    }
}

struct SectionHeader<Trailing: View>: View {
    let title: String
    let trailing: Trailing

    init(_ title: String, @ViewBuilder trailing: () -> Trailing) {
        self.title = title
        self.trailing = trailing()
    }

    var body: some View {
        HStack(alignment: .center, spacing: 10) {
            Text(title.uppercased())
                .font(.system(size: 12, weight: .semibold))
                .tracking(1.6)
                .foregroundColor(Theme.mute)
            Spacer()
            trailing
        }
        .padding(.top, 14)
    }
}

extension SectionHeader where Trailing == EmptyView {
    init(_ title: String) {
        self.init(title) { EmptyView() }
    }
}

struct Chip: View {
    let text: String
    var color: Color = Theme.mute

    var body: some View {
        Text(text)
            .font(.system(size: 10, weight: .semibold))
            .tracking(0.5)
            .padding(.horizontal, 7)
            .padding(.vertical, 2)
            .foregroundColor(color)
            .overlay(Capsule().stroke(color.opacity(0.55), lineWidth: 1))
    }
}

struct LedButton: View {
    let title: String
    let on: Bool
    let color: Color
    let action: () -> Void

    init(_ title: String, on: Bool, color: Color, action: @escaping () -> Void) {
        self.title = title
        self.on = on
        self.color = color
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            HStack(spacing: 6) {
                Circle()
                    .fill(on ? color : Theme.ledOff)
                    .frame(width: 7, height: 7)
                    .shadow(color: on ? color : .clear, radius: 4)
                Text(title)
                    .font(.system(size: 11, weight: .bold))
                Spacer(minLength: 0)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 5)
            .frame(maxWidth: .infinity)
            .foregroundColor(on ? Theme.text : Theme.dim)
            .background(RoundedRectangle(cornerRadius: 6).fill(Color(hex: 0x121513)))
            .overlay(RoundedRectangle(cornerRadius: 6).stroke(on ? color.opacity(0.45) : Theme.line2, lineWidth: 1))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

struct ModeSegment: View {
    let options: [String]
    let value: String
    let onPick: (String) -> Void

    func tint(_ o: String) -> Color {
        let l = o.lowercased()
        if l.hasPrefix("line") { return Theme.blue }
        if l.hasPrefix("inst") || l.hasPrefix("guitar") { return Theme.violet }
        return Theme.white
    }

    var body: some View {
        HStack(spacing: 0) {
            ForEach(options, id: \.self) { o in
                Button { onPick(o) } label: {
                    Text(o.uppercased())
                        .font(.system(size: 11, weight: .semibold))
                        .lineLimit(1)
                        .minimumScaleFactor(0.8)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 5)
                        .foregroundColor(value == o ? tint(o) : Theme.mute)
                        .background(value == o ? Theme.panel2 : Color.clear)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
        }
        .overlay(RoundedRectangle(cornerRadius: 7).stroke(Theme.line2, lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 7))
    }
}
