/**
 * @file PdfPages.mm
 * @brief Rasterizado multipágina de PDF con PDFKit (macOS).
 *
 * Renderiza cada página del documento con Core Graphics sobre un búfer
 * `QImage` ARGB32 premultiplicado, aplicando la escala DPI/72 y una
 * transformación que invierte el eje Y para pasar del sistema de coordenadas de
 * PDF (origen abajo) al de Qt (origen arriba). Opcionalmente devuelve el tamaño
 * de cada página en milímetros, que la importación usa para escalar el
 * resultado al tamaño real en la placa. En otras plataformas la función es un
 * stub que devuelve un vector vacío.
 */
#include "PdfPages.h"

#ifdef __APPLE__
#import <PDFKit/PDFKit.h>
#import <AppKit/AppKit.h>
#include <CoreGraphics/CoreGraphics.h>

/**
 * @brief Renderiza todas las páginas de un PDF a imágenes Qt.
 *
 * Abre el documento con `PDFDocument`, calcula la escala `dpi/72` y, por cada
 * página, deriva el tamaño en píxeles del `MediaBox`, crea la imagen, obtiene un
 * `CGContext` sobre sus bits (`kCGImageAlphaPremultipliedFirst` con orden de
 * bytes little-endian, equivalente a `QImage::Format_ARGB32_Premultiplied`),
 * pinta el fondo blanco, aplica `scale·translate(0, alto)·scale(1,-1)` y dibuja
 * la página con `drawWithBox:`. Al finalizar libera contexto y espacio de color.
 *
 * @param path Ruta del archivo PDF.
 * @param dpi Resolución de rasterizado; 200 ppp es el valor usado por la
 *        importación.
 * @param pageSizesMm Si no es nulo, recibe el tamaño de cada página en mm
 *        (`puntos · 25.4 / 72`) en el mismo orden que las imágenes.
 * @return Imágenes de las páginas en orden; vector vacío si el documento no se
 *         puede abrir o no tiene páginas.
 * @note El tamaño de píxeles se fuerza a un mínimo de 1 para no crear imágenes
 *       inválidas con páginas de área cero.
 * @note Usa `CropBox` (con fallback lógico a `MediaBox`) y valida `dpi > 0`.
 *       Bajo MRC el `PDFDocument` se libera explícitamente al terminar.
 */
QVector<QImage> renderPdfPages(const QString& path, int dpi, QVector<QSizeF>* pageSizesMm) {
    QVector<QImage> pages;
    if (dpi <= 0) return pages;
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.toUtf8().constData()];
        NSURL* url = [NSURL fileURLWithPath:nsPath];
        PDFDocument* document = [[PDFDocument alloc] initWithURL:url];
        if (!document) return pages;
        const int count = static_cast<int>([document pageCount]);
        const double scale = dpi / 72.0;
        for (int i = 0; i < count; ++i) {
            PDFPage* page = [document pageAtIndex:i];
            if (!page) continue;
            NSRect box = [page boundsForBox:kPDFDisplayBoxCropBox];
            if (box.size.width <= 0.0 || box.size.height <= 0.0)
                box = [page boundsForBox:kPDFDisplayBoxMediaBox];
            const int width = qMax(1, static_cast<int>(std::lround(box.size.width * scale)));
            const int height = qMax(1, static_cast<int>(std::lround(box.size.height * scale)));
            QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
            CGContextRef context = CGBitmapContextCreate(
                image.bits(), width, height, 8, image.bytesPerLine(), colorSpace,
                kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
            if (!context) {
                CGColorSpaceRelease(colorSpace);
                continue;
            }
            CGContextSetRGBFillColor(context, 1, 1, 1, 1);
            CGContextFillRect(context, CGRectMake(0, 0, width, height));
            CGContextScaleCTM(context, scale, scale);
            CGContextTranslateCTM(context, 0, box.size.height);
            CGContextScaleCTM(context, 1, -1);
            [page drawWithBox:kPDFDisplayBoxCropBox toContext:context];
            CGContextRelease(context);
            CGColorSpaceRelease(colorSpace);
            // PDFKit dibuja con el origen en la esquina inferior izquierda;
            // QImage usa la esquina superior izquierda. El espejo vertical
            // deja el texto y la geometría en la orientación correcta.
            image = image.flipped(Qt::Vertical);
            pages.push_back(image);
            if (pageSizesMm)
                pageSizesMm->push_back(QSizeF(box.size.width * 25.4 / 72.0,
                                              box.size.height * 25.4 / 72.0));
        }
        [document release];
    }
    return pages;
}
#else
/**
 * @brief Implementación vacía para plataformas sin PDFKit.
 *
 * @return Siempre un vector vacío, de modo que el llamador caiga al método
 *         alternativo de rasterizado (por ejemplo `sips` o un importador
 *         externo).
 * @note Mantiene la firma de PdfPages.h para que la GUI compile igual en
 *       Windows y Linux.
 */
QVector<QImage> renderPdfPages(const QString&, int, QVector<QSizeF>*) { return {}; }
#endif
