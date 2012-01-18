/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the QtDBus module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qdbusxmlparser_p.h"
#include "qdbusutil_p.h"

#include <QtCore/qmap.h>
#include <QtCore/qvariant.h>
#include <QtCore/qtextstream.h>
#include <QtCore/qxmlstream.h>
#include <QtCore/qdebug.h>

#ifndef QT_NO_DBUS

//#define QDBUS_PARSER_DEBUG
#ifdef QDBUS_PARSER_DEBUG
# define qDBusParserError qDebug
#else
# define qDBusParserError if (true) {} else qDebug
#endif

QT_BEGIN_NAMESPACE

static bool parseArg(const QXmlStreamAttributes &attributes, QDBusIntrospection::Argument &argData)
{
    const QString argType = attributes.value(QLatin1String("type")).toString();

    bool ok = QDBusUtil::isValidSingleSignature(argType);
    if (!ok) {
        qDBusParserError("Invalid D-BUS type signature '%s' found while parsing introspection",
                qPrintable(argType));
    }

    argData.name = attributes.value(QLatin1String("name")).toString();
    argData.type = argType;

    return ok;
}

static bool parseAnnotation(const QXmlStreamReader &xml, QDBusIntrospection::Annotations &annotations)
{
    Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("annotation"));

    const QXmlStreamAttributes attributes = xml.attributes();
    const QString name = attributes.value(QLatin1String("name")).toString();

    if (!QDBusUtil::isValidInterfaceName(name)) {
        qDBusParserError("Invalid D-BUS annotation '%s' found while parsing introspection",
                qPrintable(name));
        return false;
    }
    annotations.insert(name, attributes.value(QLatin1String("value")).toString());
    return true;
}

static bool parseProperty(QXmlStreamReader &xml, QDBusIntrospection::Property &propertyData,
                const QString &ifaceName)
{
    Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("property"));

    QXmlStreamAttributes attributes = xml.attributes();
    const QString propertyName = attributes.value(QLatin1String("name")).toString();
    if (!QDBusUtil::isValidMemberName(propertyName)) {
        qDBusParserError("Invalid D-BUS member name '%s' found in interface '%s' while parsing introspection",
                qPrintable(propertyName), qPrintable(ifaceName));
        xml.skipCurrentElement();
        return false;
    }

    // parse data
    propertyData.name = propertyName;
    propertyData.type = attributes.value(QLatin1String("type")).toString();

    if (!QDBusUtil::isValidSingleSignature(propertyData.type)) {
        // cannot be!
        qDBusParserError("Invalid D-BUS type signature '%s' found in property '%s.%s' while parsing introspection",
                qPrintable(propertyData.type), qPrintable(ifaceName),
                qPrintable(propertyName));
    }

    const QString access = attributes.value(QLatin1String("access")).toString();
    if (access == QLatin1String("read"))
        propertyData.access = QDBusIntrospection::Property::Read;
    else if (access == QLatin1String("write"))
        propertyData.access = QDBusIntrospection::Property::Write;
    else if (access == QLatin1String("readwrite"))
        propertyData.access = QDBusIntrospection::Property::ReadWrite;
    else {
        qDBusParserError("Invalid D-BUS property access '%s' found in property '%s.%s' while parsing introspection",
                qPrintable(access), qPrintable(ifaceName),
                qPrintable(propertyName));
        return false;       // invalid one!
    }

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("annotation")) {
            parseAnnotation(xml, propertyData.annotations);
        } else if (xml.prefix().isEmpty()) {
            qDBusParserError() << "Unknown element" << xml.name() << "while checking for annotations";
        }
        xml.skipCurrentElement();
    }

    if (!xml.isEndElement() || xml.name() != QLatin1String("property")) {
        qDBusParserError() << "Invalid property specification" << xml.tokenString() << xml.name();
        return false;
    }

    return true;
}

static bool parseMethod(QXmlStreamReader &xml, QDBusIntrospection::Method &methodData,
        const QString &ifaceName)
{
    Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("method"));

    const QXmlStreamAttributes attributes = xml.attributes();
    const QString methodName = attributes.value(QLatin1String("name")).toString();
    if (!QDBusUtil::isValidMemberName(methodName)) {
        qDBusParserError("Invalid D-BUS member name '%s' found in interface '%s' while parsing introspection",
                qPrintable(methodName), qPrintable(ifaceName));
        return false;
    }

    methodData.name = methodName;

    QDBusIntrospection::Arguments outArguments;
    QDBusIntrospection::Arguments inArguments;
    QDBusIntrospection::Annotations annotations;

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("annotation")) {
            parseAnnotation(xml, annotations);
        } else if (xml.name() == QLatin1String("arg")) {
            const QXmlStreamAttributes attributes = xml.attributes();
            const QString direction = attributes.value(QLatin1String("direction")).toString();
            QDBusIntrospection::Argument argument;
            if (!attributes.hasAttribute(QLatin1String("direction"))
                    || direction == QLatin1String("in")) {
                parseArg(attributes, argument);
                inArguments << argument;
            } else if (direction == QLatin1String("out")) {
                parseArg(attributes, argument);
                outArguments << argument;
            }
        } else if (xml.prefix().isEmpty()) {
            qDBusParserError() << "Unknown element" << xml.name() << "while checking for method arguments";
        }
        xml.skipCurrentElement();
    }

    methodData.inputArgs = inArguments;
    methodData.outputArgs = outArguments;
    methodData.annotations = annotations;

    return true;
}


static bool parseSignal(QXmlStreamReader &xml, QDBusIntrospection::Signal &signalData,
        const QString &ifaceName)
{
    Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("signal"));

    const QXmlStreamAttributes attributes = xml.attributes();
    const QString signalName = attributes.value(QLatin1String("name")).toString();

    if (!QDBusUtil::isValidMemberName(signalName)) {
        qDBusParserError("Invalid D-BUS member name '%s' found in interface '%s' while parsing introspection",
                qPrintable(signalName), qPrintable(ifaceName));
        return false;
    }

    signalData.name = signalName;


    QDBusIntrospection::Arguments arguments;
    QDBusIntrospection::Annotations annotations;

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("annotation")) {
            parseAnnotation(xml, annotations);
        } else if (xml.name() == QLatin1String("arg")) {
            const QXmlStreamAttributes attributes = xml.attributes();
            QDBusIntrospection::Argument argument;
            if (!attributes.hasAttribute(QLatin1String("direction")) ||
                attributes.value(QLatin1String("direction")) == QLatin1String("out")) {
                parseArg(attributes, argument);
                arguments << argument;
            }
        } else {
            qDBusParserError() << "Unknown element" << xml.name() << "while checking for signal arguments";
        }
        xml.skipCurrentElement();
    }

    signalData.outputArgs = arguments;
    signalData.annotations = annotations;

    return true;
}

static void readInterface(QXmlStreamReader &xml, QDBusIntrospection::Object *objData,
        QDBusIntrospection::Interfaces *interfaces)
{
    const QString ifaceName = xml.attributes().value(QLatin1String("name")).toString();
    if (!QDBusUtil::isValidInterfaceName(ifaceName)) {
        qDBusParserError("Invalid D-BUS interface name '%s' found while parsing introspection",
                qPrintable(ifaceName));
        return;
    }

    objData->interfaces.append(ifaceName);

    QDBusIntrospection::Interface *ifaceData = new QDBusIntrospection::Interface;
    ifaceData->name = ifaceName;

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("method")) {
            QDBusIntrospection::Method methodData;
            if (parseMethod(xml, methodData, ifaceName))
                ifaceData->methods.insert(methodData.name, methodData);
        } else if (xml.name() == QLatin1String("signal")) {
            QDBusIntrospection::Signal signalData;
            if (parseSignal(xml, signalData, ifaceName))
                ifaceData->signals_.insert(signalData.name, signalData);
        } else if (xml.name() == QLatin1String("property")) {
            QDBusIntrospection::Property propertyData;
            if (parseProperty(xml, propertyData, ifaceName))
                ifaceData->properties.insert(propertyData.name, propertyData);
        } else if (xml.name() == QLatin1String("annotation")) {
            parseAnnotation(xml, ifaceData->annotations);
            xml.skipCurrentElement(); // skip over annotation object
        } else {
            if (xml.prefix().isEmpty()) {
                qDBusParserError() << "Unknown element while parsing interface" << xml.name();
            }
            xml.skipCurrentElement();
        }
    }

    interfaces->insert(ifaceName, QSharedDataPointer<QDBusIntrospection::Interface>(ifaceData));

    if (!xml.isEndElement() || xml.name() != QLatin1String("interface")) {
        qDBusParserError() << "Invalid Interface specification";
    }
}

static void readNode(const QXmlStreamReader &xml, QDBusIntrospection::Object *objData, int nodeLevel)
{
    const QString objName = xml.attributes().value(QLatin1String("name")).toString();
    const QString fullName = objData->path.endsWith(QLatin1Char('/'))
                                ? (objData->path + objName)
                                : QString(objData->path + QLatin1Char('/') + objName);
    if (!QDBusUtil::isValidObjectPath(fullName)) {
        qDBusParserError("Invalid D-BUS object path '%s' found while parsing introspection",
                 qPrintable(fullName));
        return;
    }

    if (nodeLevel > 0)
        objData->childObjects.append(objName);
}

QDBusXmlParser::QDBusXmlParser(const QString& service, const QString& path,
                               const QString& xmlData)
    : m_service(service), m_path(path), m_object(new QDBusIntrospection::Object)
{
//    qDBusParserError() << "parsing" << xmlData;

    m_object->service = m_service;
    m_object->path = m_path;

    QXmlStreamReader xml(xmlData);

    int nodeLevel = -1;

    while (!xml.atEnd()) {
        xml.readNext();

        switch (xml.tokenType()) {
        case QXmlStreamReader::StartElement:
            if (xml.name() == QLatin1String("node")) {
                readNode(xml, m_object, ++nodeLevel);
            } else if (xml.name() == QLatin1String("interface")) {
                readInterface(xml, m_object, &m_interfaces);
            } else {
                if (xml.prefix().isEmpty()) {
                    qDBusParserError() << "skipping unknown element" << xml.name();
                }
                xml.skipCurrentElement();
            }
            break;
        case QXmlStreamReader::EndElement:
            if (xml.name() == QLatin1String("node")) {
                --nodeLevel;
            } else {
                qDBusParserError() << "Invalid Node declaration" << xml.name();
            }
            break;
        case QXmlStreamReader::StartDocument:
        case QXmlStreamReader::EndDocument:
        case QXmlStreamReader::DTD:
            // not interested
            break;
        case QXmlStreamReader::Comment:
            // ignore comments and processing instructions
            break;
        default:
            qDBusParserError() << "unknown token" << xml.name() << xml.tokenString();
            break;
        }
    }

    if (xml.hasError()) {
        qDBusParserError() << "xml error" << xml.errorString() << "doc" << xmlData;
    }
}

QT_END_NAMESPACE

#endif // QT_NO_DBUS
