/*
 * encryptor.h - the source file of Encryptor class
 *
 * Copyright (C) 2014-2015 Symeon Huang <hzwhuang@gmail.com>
 *
 * This file is part of the libQtShadowsocks.
 *
 * libQtShadowsocks is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * libQtShadowsocks is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libQtShadowsocks; see the file LICENSE. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <QDebug>
#include "encryptor.h"

using namespace QSS;

Encryptor::Encryptor(QObject *parent) :
    QObject(parent)
{
    enCipher = NULL;
    deCipher = NULL;
}

//define static member variables
Encryptor::TYPE Encryptor::type;
QByteArray Encryptor::method;
QByteArray Encryptor::password;
QVector<quint8> Encryptor::encTable;
QVector<quint8> Encryptor::decTable;
int Encryptor::keyLen = 0;
int Encryptor::ivLen = 0;
QByteArray Encryptor::key;

void Encryptor::reset()
{
    if (enCipher != NULL) {
        enCipher->deleteLater();
        enCipher = NULL;
    }
    if (deCipher != NULL) {
        deCipher->deleteLater();
        deCipher = NULL;
    }
}

bool Encryptor::initialise(const QString &m, const QString &pwd)
{
    method = m.toUpper().toLocal8Bit();//local8bit or utf-8?
    password = pwd.toLocal8Bit();

    if (method == "TABLE") {
        type = TABLE;
        tableInit();
        return true;
    }

    type = CIPHER;
    if (method.contains("BF")) {
        method = "Blowfish/CFB";
    }
    else if (method.contains("CAST5")) {
        method = "CAST-128/CFB";
    }
    else if (method.contains("SALSA20")) {
        method = "Salsa20";
    }
    else if (method.contains("CHACHA20")) {
        method = "ChaCha";
    }
    else {
        method.replace("-C", "/C");//i.e. -CFB to /CFB
    }

    QVector<int> ki = Cipher::keyIvMap.value(method);
    if (ki.isEmpty() || !Cipher::isSupported(method)) {
        qCritical() << "Abort. The method" << m.toLocal8Bit() << "is not supported.";
        return false;
    }
    keyLen = ki[0];
    ivLen = ki[1];
    evpBytesToKey();
    return true;
}

QString Encryptor::getInternalMethodName()
{
    return QString(method);
}

void Encryptor::tableInit()
{
    quint32 i;
    quint64 key = 0;

    encTable.fill(0, 256);
    decTable.fill(0, 256);
    QByteArray digest = Cipher::md5Hash(password);

    for (i = 0; i < 8; ++i) {
        key += (quint64(digest.at(i)) << (8 * i));
    }
    for (i = 0; i < 256; ++i) {
        encTable[i] = i;
    }
    for (i = 1; i < 1024; ++i) {
        encTable = mergeSort(encTable, i, key);
    }
    for (i = 0; i < 256; ++i) {
        decTable[encTable[i]] = i;
    }
}

QVector<quint8> Encryptor::mergeSort(const QVector<quint8> &array, quint32 salt, quint64 key)
{
    int length = array.size();

    if (length <= 1) {
        return array;
    }

    int middle = length / 2;
    QVector<quint8> left = array.mid(0, middle);
    QVector<quint8> right = array.mid(middle);

    left = mergeSort(left, salt, key);
    right = mergeSort(right, salt, key);

    int leftptr = 0;
    int rightptr = 0;

    QVector<quint8> sorted;
    sorted.fill(0, length);
    for (int i = 0; i < length; ++i) {
        if (rightptr == right.size() || (leftptr < left.size() && randomCompare(left[leftptr], right[rightptr], salt, key) <= 0)) {
            sorted[i] = left[leftptr];
            leftptr++;
        }
        else if (leftptr == left.size() || (rightptr < right.size() && randomCompare(right[rightptr], left[leftptr], salt, key) <= 0)) {
            sorted[i] = right[rightptr];
            rightptr++;
        }
    }
    return sorted;
}

int Encryptor::randomCompare(const quint8 &x, const quint8 &y, const quint32 &i, const quint64 &a)
{
    return a % (x + i) - a % (y + i);
}

void Encryptor::evpBytesToKey()
{
    QVector<QByteArray> m;
    QByteArray data;
    int i = 0;

    while (m.size() < keyLen + ivLen) {
        if (i == 0) {
            data = password;
        }
        else {
            data = m[i - 1] + password;
        }
        m.append(Cipher::md5Hash(data));
        i++;
    }
    QByteArray ms;
    for (QVector<QByteArray>::ConstIterator it = m.begin(); it != m.end(); ++it) {
        ms.append(*it);
    }

    key = ms.mid(0, keyLen);
}

QByteArray Encryptor::encrypt(const QByteArray &in)
{
    QByteArray out;
    QByteArray iv = Cipher::randomIv(ivLen);

    switch (type) {
    case TABLE:
        out.resize(in.size());
        for (int i = 0; i < in.size(); ++i) {
            out[i] = encTable.at(in[i]);
        }
        break;
    case CIPHER:
        if (enCipher == NULL) {
            enCipher = new Cipher(method, key, iv, true, this);
            out = iv + enCipher->update(in);
        }
        else {
            out = enCipher->update(in);
        }
        break;
    default:
        qWarning() << "Unknown encryption type";
    }

    return out;
}

QByteArray Encryptor::decrypt(const QByteArray &in)
{
    QByteArray out;

    switch (type) {
    case TABLE:
        out.resize(in.size());
        for (int i = 0; i < in.size(); ++i) {
            out[i] = decTable.at(in[i]);
        }
        break;
    case CIPHER:
        if (deCipher == NULL) {
            deCipher = new Cipher(method, key, in.mid(0, ivLen), false, this);
            out = deCipher->update(in.mid(ivLen));
        }
        else {
            out = deCipher->update(in);
        }
        break;
    default:
        qWarning() << "Unknown decryption type";
    }

    return out;
}

QByteArray Encryptor::encryptAll(const QByteArray &in)
{
    QByteArray out;
    QByteArray iv = Cipher::randomIv(ivLen);

    switch (type) {
    case TABLE:
        out = QByteArray(in.size(), '0');
        for (int i = 0; i < in.size(); ++i) {
            out[i] = encTable.at(in[i]);
        }
        break;
    case CIPHER:
        if (enCipher != NULL) {
            enCipher->deleteLater();
        }
        enCipher = new Cipher(method, key, iv, true, this);
        out = iv + enCipher->update(in);
        break;
    default:
        qWarning() << "Unknown encryption type";
    }

    return out;
}

QByteArray Encryptor::decryptAll(const QByteArray &in)
{
    QByteArray out;

    switch (type) {
    case TABLE:
        out = QByteArray(in.size(), '0');
        for (int i = 0; i < in.size(); ++i) {
            out[i] = decTable.at(in[i]);
        }
        break;
    case CIPHER:
        if (deCipher != NULL) {
            deCipher->deleteLater();
        }
        deCipher = new Cipher(method, key, in.mid(0, ivLen), false, this);
        out = deCipher->update(in.mid(ivLen));
        break;
    default:
        qWarning() << "Unknown decryption type";
    }

    return out;
}

bool Encryptor::selfTest()
{
    QByteArray test("barfoo!"), test2("Hello World!"), test3("libShadowsocks!");
    QByteArray res  = decrypt(encrypt(test)),
               res2 = decrypt(encrypt(test2)),
               res3 = decrypt(encrypt(test3));
    reset();
    return test == res && test2 == res2 && test3 == res3;
}
