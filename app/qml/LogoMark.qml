// LX-Logo (dunkles Quadrat, „L“ weiß, „X“ gelb) – dieselbe Grafik wie das App-Symbol
import QtQuick

Image {
    property int size: 28
    width: size
    height: size
    source: "qrc:/resources/loggx-256.png"
    sourceSize.width: size * 2
    sourceSize.height: size * 2
    smooth: true
    mipmap: true
}
