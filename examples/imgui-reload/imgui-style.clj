(defn FColor
  [r g b a]
  (Color (* 255 r) (* 255 g) (* 255 b) (* 255 a)))

(defn applyStyle []
  [(Float2 15 15) (GUI.Style ImGuiStyle.WindowPadding)
   5.0 (GUI.Style ImGuiStyle.WindowRounding)
   (Float2 5 5) (GUI.Style ImGuiStyle.FramePadding)
   (Float2 12 8) (GUI.Style ImGuiStyle.ItemSpacing)
   (Float2 8 6) (GUI.Style ImGuiStyle.ItemInnerSpacing)
   25.0 (GUI.Style ImGuiStyle.IndentSpacing)
   15.0 (GUI.Style ImGuiStyle.ScrollbarSize)
   9.0 (GUI.Style ImGuiStyle.ScrollbarRounding)
   5.0 (GUI.Style ImGuiStyle.GrabMinSize)
   3.0 (GUI.Style ImGuiStyle.GrabRounding)
   (FColor 0.80 0.80 0.83 1.00) (GUI.Style ImGuiStyle.TextColor)
   (FColor 0.24 0.23 0.29 1.00) (GUI.Style ImGuiStyle.TextDisabledColor)
   (FColor 0.06 0.05 0.07 1.00) (GUI.Style ImGuiStyle.WindowBgColor)
   (FColor 0.07 0.07 0.09 1.00) (GUI.Style ImGuiStyle.ChildBgColor)
   (FColor 0.07 0.07 0.09 1.00) (GUI.Style ImGuiStyle.PopupBgColor)
   (FColor 0.80 0.80 0.83 0.88) (GUI.Style ImGuiStyle.BorderColor)
   (FColor 0.92 0.91 0.88 0.00) (GUI.Style ImGuiStyle.BorderShadowColor)
   (FColor 0.10 0.09 0.12 1.00) (GUI.Style ImGuiStyle.FrameBgColor)
   (FColor 0.24 0.23 0.29 1.00) (GUI.Style ImGuiStyle.FrameBgHoveredColor)
   (FColor 0.56 0.56 0.58 1.00) (GUI.Style ImGuiStyle.FrameBgActiveColor)
   (FColor 0.10 0.09 0.12 1.00) (GUI.Style ImGuiStyle.TitleBgColor)
   (FColor 1.00 0.98 0.95 0.75) (GUI.Style ImGuiStyle.TitleBgCollapsedColor)
   (FColor 0.07 0.07 0.09 1.00) (GUI.Style ImGuiStyle.TitleBgActiveColor)
   (FColor 0.10 0.09 0.12 1.00) (GUI.Style ImGuiStyle.MenuBarBgColor)
   (FColor 0.10 0.09 0.12 1.00) (GUI.Style ImGuiStyle.ScrollbarBgColor)
   (FColor 0.80 0.80 0.83 0.31) (GUI.Style ImGuiStyle.ScrollbarGrabColor)
   (FColor 0.56 0.56 0.58 1.00) (GUI.Style ImGuiStyle.ScrollbarGrabHoveredColor)
   (FColor 0.06 0.05 0.07 1.00) (GUI.Style ImGuiStyle.ScrollbarGrabActiveColor)
   (FColor 0.80 0.80 0.83 0.31) (GUI.Style ImGuiStyle.CheckMarkColor)
   (FColor 0.80 0.80 0.83 0.31) (GUI.Style ImGuiStyle.SliderGrabColor)
   (FColor 0.06 0.05 0.07 1.00) (GUI.Style ImGuiStyle.SliderGrabActiveColor)
   (FColor 0.10 0.09 0.12 1.00) (GUI.Style ImGuiStyle.ButtonColor)
   (FColor 0.24 0.23 0.29 1.00) (GUI.Style ImGuiStyle.ButtonHoveredColor)
   (FColor 0.56 0.56 0.58 1.00) (GUI.Style ImGuiStyle.ButtonActiveColor)
   (FColor 0.10 0.09 0.12 1.00) (GUI.Style ImGuiStyle.HeaderColor)
   (FColor 0.56 0.56 0.58 1.00) (GUI.Style ImGuiStyle.HeaderHoveredColor)
   (FColor 0.06 0.05 0.07 1.00) (GUI.Style ImGuiStyle.HeaderActiveColor)
   (FColor 0.00 0.00 0.00 0.00) (GUI.Style ImGuiStyle.ResizeGripColor)
   (FColor 0.56 0.56 0.58 1.00) (GUI.Style ImGuiStyle.ResizeGripHoveredColor)
   (FColor 0.06 0.05 0.07 1.00) (GUI.Style ImGuiStyle.ResizeGripActiveColor)
   (FColor 0.40 0.39 0.38 0.63) (GUI.Style ImGuiStyle.PlotLinesColor)
   (FColor 0.25 1.00 0.00 1.00) (GUI.Style ImGuiStyle.PlotLinesHoveredColor)
   (FColor 0.40 0.39 0.38 0.63) (GUI.Style ImGuiStyle.PlotHistogramColor)
   (FColor 0.25 1.00 0.00 1.00) (GUI.Style ImGuiStyle.PlotHistogramHoveredColor)
   (FColor 0.25 1.00 0.00 0.43) (GUI.Style ImGuiStyle.TextSelectedBgColor)])
