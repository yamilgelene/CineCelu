# CineCelu

App chica para Mac que copia los videos de la cámara de un iPhone a la carpeta que elijas. Lee los archivos directo del teléfono, no pasa por Captura de Imagen.

Sirve en macOS Sonoma 14.7 o más nuevo, con chip Apple. No hay que instalar nada aparte: usa el soporte de iPhone que ya viene en el sistema.

## Compilar

Hace falta Xcode o las Command Line Tools.

```bash
./build.sh
```

Eso arma `~/Applications/CineCelu.app`.

## Usarla

Enchufá el iPhone, desbloquealo y tocá Confiar. Elegí la carpeta de destino y copiá. Si un archivo del destino ya tiene el mismo tamaño, no lo vuelve a bajar. Borrar del teléfono solo toca los que coinciden con la copia local.

Si Fotos en iCloud tenía el video, puede seguir viéndose en el teléfono aunque el archivo de la cámara ya no esté.

La primera vez, si macOS dice que la app está dañada:

```bash
xattr -dr com.apple.quarantine /Applications/CineCelu.app
codesign --force --sign - /Applications/CineCelu.app
```
