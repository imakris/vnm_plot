#pragma once

#include <QFile>
#include <rhi/qshader.h>

#include <cmath>

namespace vnm::plot::detail {

inline QShader load_qsb(const char* alias)
{
    QFile file(QStringLiteral(":/vnm_plot/shaders/qsb/") + QString::fromLatin1(alias));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

inline bool to_int_rounded(double value, int& out)
{
    if (!std::isfinite(value)) {
        return false;
    }

    out = static_cast<int>(lround(value));
    return true;
}

inline bool to_positive_int(double value, int& out)
{
    if (!std::isfinite(value)) {
        return false;
    }

    const long rounded = lround(value);
    if (rounded <= 0) {
        return false;
    }

    out = static_cast<int>(rounded);
    return true;
}

} // namespace vnm::plot::detail
