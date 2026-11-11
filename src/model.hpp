#pragma once
#include "dsp.hpp"
#include <QJsonObject>
#include <QString>
#include <QStringList>

QString controlSocketPath();
QStringList presetNames();
murmur::Parameters presetParameters(const QString& name);
QJsonObject parametersJson(const murmur::Parameters&, bool session = true);
bool applyParameterPatch(const QJsonObject&, const murmur::Parameters& base,
                         murmur::Parameters& result, QString& error);
bool readParameters(const QString& path, murmur::Parameters&, QString& error);
bool writeParameters(const QString& path, const murmur::Parameters&, QString& error);
