# CineCelu

Copia los videos de la cámara de un iPhone a la carpeta que elijas. No usa Captura de Imagen.

Sirve en macOS Sonoma 14.7 o más nuevo, con chip Apple. No hay que instalar nada: usa el soporte de iPhone que ya viene en el sistema.

## Para qué existe

Nació en una sesión de música filmada con el iPhone en modo cine. Los videos se veían en la app Fotos del teléfono, pero Captura de Imagen en la Mac no los mostraba. Captura de Imagen lista lo que el teléfono publica por el catálogo de la cámara. Con Fotos en iCloud, ese catálogo suele traer los que ya subieron a la nube y deja afuera originales que sí están en el rollo, sobre todo los HEVC de modo cine.

Había que bajar varios videos grandes y el disco interno de la Mac estaba casi lleno. La copia tenía que ir directo a un disco externo, sin un paso intermedio.

CineCelu lee la carpeta de la cámara del iPhone y escribe en la carpeta que le indiques.

## Cómo se usa

1. Enchufá el iPhone, desbloquealo y tocá Confiar.
2. Elegí la carpeta de destino. Si esa carpeta está en el disco interno, la app avisa.
3. Marcá los videos y copiá.

Si el archivo de destino ya tiene el mismo tamaño que el del teléfono, no lo vuelve a bajar. Si se corta el cable, volvé a tocar Copiar: sigue con lo que falta.

Borrar del teléfono solo elimina un video cuando la copia local coincide en tamaño. Si Fotos en iCloud lo tenía, puede seguir apareciendo en la app aunque el archivo de la cámara ya no esté. Sacarlo de Fotos es otro paso.

## Compilar

Hace falta Xcode o las Command Line Tools.

```bash
./build.sh
```

Eso arma `~/Applications/CineCelu.app`.

La primera vez, si macOS dice que la app está dañada:

```bash
xattr -dr com.apple.quarantine /Applications/CineCelu.app
codesign --force --sign - /Applications/CineCelu.app
```
