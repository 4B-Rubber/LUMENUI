# Paper Shaders（移植片段）

- 来源：https://github.com/paper-design/shaders（主页 https://shaders.paper.design）
- 版本：main @ 43cd68d（2026-09-17）
- 许可：Apache-2.0，原文见本目录 `LICENSE`，署名见 `NOTICE`。

## 移植内容

`src/core/shaders/lumen_shader.hlsl` 中的 `MeshGradient` 与 `LiquidMetal` 两个函数分别移植自
`packages/shaders/src/shaders/mesh-gradient.ts` 与 `liquid-metal.ts` 的片元着色器，
辅助函数 `Rotate2`、`SimplexNoise` 移植自 `shader-utils.ts`。

## LUMEN 的修改

- GLSL ES 3.0 改写为 HLSL（ps_4_0，Direct2D 零输入自定义效果）；
- 常量改为 LUMEN 的 cbuffer 布局（`ShaderConstants`），颜色最多 8 个；
- 输出乘以 LUMEN 光效强度并叠加 LUMEN 胶片颗粒，未移植 Paper 的 grainMixer / grainOverlay；
- MeshGradient 未设调色板时以 tint 的四级明度绘制（单色回退）；
- LiquidMetal 仅保留全幅模式（无图片蒙版、无预设形状），叠色取调色板第 1 色。

LUMEN 不再分发 Paper Shaders 的其他代码。分发 LUMEN 源码或二进制时请保留本目录。
