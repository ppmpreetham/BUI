"""{{title}}: Qt-style code. Only the import differs from PySide or PyQt; neither is installed."""

import sys

from blenderui.qtsyntax import (
    QApplication,
    QComboBox,
    QFormLayout,
    QLabel,
    QProgressBar,
    QPushButton,
    QSlider,
    Qt,
    QVBoxLayout,
    QWidget,
)


def main() -> int:
    app = QApplication(sys.argv)
    window = QWidget()
    window.setWindowTitle("{{title}}")
    layout = QVBoxLayout(window)

    form = QFormLayout()
    kind = QComboBox()
    kind.addItems(["glTF", "FBX", "OBJ"])
    quality = QSlider(Qt.Horizontal)
    quality.setRange(0, 100)
    form.addRow("Format:", kind)
    form.addRow("Quality:", quality)
    layout.addLayout(form)

    bar = QProgressBar()
    quality.valueChanged.connect(bar.setValue)
    layout.addWidget(bar)

    status = QLabel("Ready.")
    go = QPushButton("Export")
    go.clicked.connect(lambda: status.setText("Exporting as " + kind.currentText()))
    layout.addWidget(status)
    layout.addWidget(go)

    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
