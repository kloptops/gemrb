/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "DirectoryImporter.h"

#include "globals.h"

#include "Interface.h"
#include "ResourceDesc.h"
#include "Streams/FileStream.h"

using namespace GemRB;

bool DirectoryImporter::Open(const char *dir, const char *desc)
{
	if (!dir_exists(dir))
		return false;

	description = desc;
	if (strlcpy(path, dir, _MAX_PATH) >= _MAX_PATH) {
		Log(ERROR, "DirectoryImporter", "Directory with too long path: {}!", dir);
		return false;
	}
	return true;
}

static bool FindIn(const char *Path, StringView ResRef, const char *Type)
{
	char p[_MAX_PATH] = {0};
	std::string f(ResRef.c_str(), ResRef.length());
	StringToLower(f);

	return PathJoinExt(p, Path, f.c_str(), Type);
}

static FileStream *SearchIn(const char * Path, StringView ResRef, const char *Type)
{
	char p[_MAX_PATH] = {0};
	std::string f(ResRef.c_str(), ResRef.length());
	StringToLower(f);

	if (!PathJoinExt(p, Path, f.c_str(), Type))
		return nullptr;

	return FileStream::OpenFile(p);
}

bool DirectoryImporter::HasResource(StringView resname, SClass_ID type)
{
	return FindIn( path, resname, core->TypeExt(type) );
}

bool DirectoryImporter::HasResource(StringView resname, const ResourceDesc &type)
{
	return FindIn( path, resname, type.GetExt() );
}

DataStream* DirectoryImporter::GetResource(StringView resname, SClass_ID type)
{
	return SearchIn( path, resname, core->TypeExt(type) );
}

DataStream* DirectoryImporter::GetResource(StringView resname, const ResourceDesc &type)
{
	return SearchIn( path, resname, type.GetExt() );
}

bool CachedDirectoryImporter::Open(const char *dir, const char *desc)
{
	if (!DirectoryImporter::Open(dir, desc))
		return false;

	Refresh();

	return true;
}

void CachedDirectoryImporter::Refresh()
{
	cache.clear();

	DirectoryIterator it(path);
	it.SetFlags(DirectoryIterator::Files, true);
	if (!it)
		return;

	unsigned int count = 0;
	do {
		count++;
	} while (++it);

	// limit to 4k buckets
	// less than 1% of the bg2+fixpack override are of bucket length >4
	cache.init(count > 4 * 1024 ? 4 * 1024 : count, count);

	it.Rewind();

	do {
		const char *name = it.GetName();
		std::string buf = name;
		StringToLower(buf);
		if (cache.set(buf, name)) {
			Log(ERROR, "CachedDirectoryImporter", "Duplicate '{}' files in '{}' directory", buf, path);
		}
	} while (++it);
}

static std::string ConstructFilename(StringView resname, const char* ext)
{
	assert(strnlen(ext, 5) < 5);
	std::string buf(resname.c_str(), resname.length());
	StringToLower(buf);
	buf.push_back('.');
	buf += ext;
	return buf;
}

bool CachedDirectoryImporter::HasResource(StringView resname, SClass_ID type)
{
	const std::string& filename = ConstructFilename(resname, core->TypeExt(type));
	return cache.has(filename.c_str());
}

bool CachedDirectoryImporter::HasResource(StringView resname, const ResourceDesc &type)
{
	const std::string& filename = ConstructFilename(resname, type.GetExt());
	return cache.has(filename.c_str());
}

DataStream* CachedDirectoryImporter::GetResource(StringView resname, SClass_ID type)
{
	const std::string& filename = ConstructFilename(resname, core->TypeExt(type));
	const std::string *s = cache.get(filename.c_str());
	if (!s)
		return NULL;
	char buf[_MAX_PATH];
	strcpy(buf, path);
	PathAppend(buf, s->c_str());
	return FileStream::OpenFile(buf);
}

DataStream* CachedDirectoryImporter::GetResource(StringView resname, const ResourceDesc &type)
{
	const std::string& filename = ConstructFilename(resname, type.GetExt());
	const std::string *s = cache.get(filename.c_str());
	if (!s)
		return NULL;
	char buf[_MAX_PATH];
	strcpy(buf, path);
	PathAppend(buf, s->c_str());
	return FileStream::OpenFile(buf);
}

#include "plugindef.h"

GEMRB_PLUGIN(0xAB4534, "Directory Importer")
PLUGIN_CLASS(PLUGIN_RESOURCE_DIRECTORY, DirectoryImporter)
PLUGIN_CLASS(PLUGIN_RESOURCE_CACHEDDIRECTORY, CachedDirectoryImporter)
END_PLUGIN()
