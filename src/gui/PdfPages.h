#pragma once

#include <QImage>
#include <QSizeF>
#include <QString>
#include <QVector>

// Renders every page of a PDF to an image (macOS PDFKit). Returns an empty
// vector on other platforms or on error. Page sizes are in millimetres.
QVector<QImage> renderPdfPages(const QString& path, int dpi,
                               QVector<QSizeF>* pageSizesMm = nullptr);
