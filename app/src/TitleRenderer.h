// Zeichnet Titel-Clips mit Qt (Schriften, Kantenglättung). Funktioniert in
// jedem Thread, solange eine QGuiApplication existiert.
#pragma once

#include "loggx/engine/Engine.h"

loggx::TitleRasterizer qtTitleRasterizer();
