# Selection Feature

Общая логика выделения (selection), используемая canvas и другими компонентами.

## Структура

| Компонент | Назначение |
|-----------|------------|
| **LassoSelectionManager** | Маска выделения (TileGrid), регионы, рёбра. Режимы Replace/Add/Subtract. |
| **GLSelectionRenderer** | OpenGL-рендеринг выделения: полигон → триангуляция, шейдеры fill/subtract, асинхронный readback. |
| **SelectionActionPopup** | UI-попап для действий: заливка, трансформация, удаление, сброс. |

## Связь с canvas

**features/canvas/selection/** — интеграция выделения в canvas:

- **CanvasSelectionController** — оркестрирует ввод (лассо, rect, circle), использует `LassoSelectionManager` и `GLSelectionRenderer`
- **PolygonClipUtils** — утилиты для clipping полигонов

Итого: `features/selection` — общий движок; `features/canvas/selection` — canvas-специфичный контроллер и утилиты.
