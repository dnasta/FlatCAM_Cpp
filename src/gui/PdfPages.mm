#include "PdfPages.h"

#ifdef __APPLE__
#import <PDFKit/PDFKit.h>
#import <AppKit/AppKit.h>
#include <CoreGraphics/CoreGraphics.h>

QVector<QImage> renderPdfPages(const QString& path, int dpi, QVector<QSizeF>* pageSizesMm) {
    QVector<QImage> pages;
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
            const NSRect box = [page boundsForBox:kPDFDisplayBoxMediaBox];
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
            [page drawWithBox:kPDFDisplayBoxMediaBox toContext:context];
            CGContextRelease(context);
            CGColorSpaceRelease(colorSpace);
            pages.push_back(image);
            if (pageSizesMm)
                pageSizesMm->push_back(QSizeF(box.size.width * 25.4 / 72.0,
                                              box.size.height * 25.4 / 72.0));
        }
    }
    return pages;
}
#else
QVector<QImage> renderPdfPages(const QString&, int, QVector<QSizeF>*) { return {}; }
#endif
