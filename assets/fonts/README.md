# Fonts

| File | Font | Author | Licence |
|---|---|---|---|
| `Roboto-Medium.ttf` | Roboto Medium | Christian Robertson (Google) | Apache License 2.0, see `LICENSE-Apache-2.0.txt` |

Roboto: <https://fonts.google.com/specimen/Roboto>. This copy is the one distributed with
Dear ImGui (`misc/fonts`).

The Apache License 2.0 allows using, modifying and redistributing the font, including in
commercial games. Ship `LICENSE-Apache-2.0.txt` alongside it (it is copied to the build output
with the rest of `assets/`).

To use another font, drop its `.ttf`/`.otf` file in this folder and load it with
`render2d::Font::load(gpu, core::assetPath("fonts/YourFont.ttf"))`. Check its licence first;
fonts under the SIL Open Font License (most of Google Fonts) are also free to ship with games.
