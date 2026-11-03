/**
 * @file PdfPages.h
 * @brief Rasterizado multipágina de PDF con PDFKit (macOS).
 */
#pragma once

#include <QImage>
#include <QSizeF>
#include <QString>
#include <QVector>

/**
 * @brief Renderiza todas las páginas de un PDF a imágenes.
 *
 * En macOS usa PDFKit. En otras plataformas devuelve un vector vacío.
 *
 * @param path Archivo PDF.
 * @param dpi Resolución de render.
 * @param pageSizesMm Si no es nulo, recibe el tamaño de cada página en mm.
 * @return Imágenes en orden de página.
 */
QVector<QImage> renderPdfPages(const QString& path, int dpi,
                               QVector<QSizeF>* pageSizesMm = nullptr);
