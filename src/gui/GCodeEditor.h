/**
 * @file GCodeEditor.h
 * @brief Editor de G-code con resaltado de sintaxis.
 */
#pragma once

#include <QDialog>
#include <QString>

/**
 * @brief Abre el editor de G-code para un archivo.
 * @param path Archivo a editar.
 * @param parent Widget padre.
 */
void openGCodeEditor(const QString& path, QWidget* parent = nullptr);
