// ampc_tagger.el --- Asynchronous Music Player Controller Tagger

// Copyright (C) 2012 Free Software Foundation, Inc.

// Author: Christopher Schmidt <christopher@ch.ristopher.com>
// Maintainer: Christopher Schmidt <christopher@ch.ristopher.com>
// Created: 2012-07-17

// This file is part of ampc.

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <iostream>
#include <sstream>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/id3v1genres.h>

std::wstring const version=L"0.1";
std::locale original_wcout_locale;

bool get(std::string const& file)
{
    using namespace TagLib;

    FileRef file_ref(file.c_str());
    Tag* tag;
    if(file_ref.isNull() || !(tag=file_ref.tag()))
    {
        std::wcerr << L"ERROR: Failed opening file." << std::endl;
        return true;
    }

    std::wcout << L"Title: " << tag->title().toWString() << std::endl <<
        L"Artist: " << tag->artist().toWString() << std::endl <<
        L"Album: " << tag->album().toWString() << std::endl <<
        L"Comment: " << tag->comment().toWString() << std::endl <<
        L"Genre: " << tag->genre().toWString() << std::endl;
    if(tag->year())
    {
        std::wcout << L"Year: ";
        std::locale new_locale=std::wcout.imbue(original_wcout_locale);
        std::wcout << tag->year();
        std::wcout.imbue(new_locale);
        std::wcout << std::endl;
    }
    if(tag->track())
    {
        std::wcout << L"Track: ";
        std::locale new_locale=std::wcout.imbue(original_wcout_locale);
        std::wcout << tag->track();
        std::wcout.imbue(new_locale);
        std::wcout << std::endl;
    }

    return false;
}

bool set(std::string const& file)
{
    using namespace TagLib;

    FileRef file_ref(file.c_str());
    Tag* tag;
    if(file_ref.isNull() || !(tag=file_ref.tag()))
    {
        std::wcerr << L"ERROR: Failed opening file." << std::endl;
        return true;
    }

    for(;;)
    {
        if(!std::wcin)
        {
            std::wcerr << L"ERROR: invalid input data." << std::endl;
            return true;
        }

        std::wstring tag_to_set;
        getline(std::wcin, tag_to_set);
        if(tag_to_set == L"")
        {
            break;
        }

        std::wstring value;
        getline(std::wcin, value);

        std::wcout << L"Setting " << tag_to_set <<
            L" to " << value << std::endl;

        if(tag_to_set == L"Title")
        {
            tag->setTitle(value);
        }
        else if(tag_to_set == L"Artist")
        {
            tag->setArtist(value);
        }
        else if(tag_to_set == L"Album")
        {
            tag->setAlbum(value);
        }
        else if(tag_to_set == L"Comment")
        {
            tag->setComment(value);
        }
        else if(tag_to_set == L"Genre")
        {
            tag->setGenre(value);
        }
        else if(tag_to_set == L"Year")
        {
            unsigned int ival;
            if(value == L"")
            {
                ival=0;
            }
            else
            {
                std::wistringstream val(value);
                val >> ival;
            }
            tag->setYear(ival);
        }
        else if(tag_to_set == L"Track")
        {
            unsigned int ival;
            if(value == L"")
            {
                ival=0;
            }
            else
            {
                std::wistringstream val(value);
                val >> ival;
            }
            tag->setTrack(ival);
        }
        else
        {
            std::wcerr << L"Unknown tag " << tag_to_set << std::endl;
            return true;
        }
    }

    if(!file_ref.save())
    {
        std::wcerr << L"Failed saving file." << std::endl;
        return true;
    }

    return false;
}

int main(int const argc, char**const argv)
{
    std::locale loc("");
    original_wcout_locale=std::wcout.imbue(loc);
    std::wcin.imbue(loc);
    std::locale::global(loc);

    std::string action;
    if(argc >= 2)
    {
        action=argv[1];
    }

    if(action == "--version")
    {
        std::wcout << version << std::endl;
        return 0;
    }
    else if(action == "--genres")
    {
        using namespace TagLib;
        StringList genres=ID3v1::genreList();
        for(StringList::ConstIterator genre=genres.begin();
            genre!=genres.end();
            ++genre)
        {
            std::wcout << genre->toWString() << std::endl;
        }
        return 0;
    }
    else if(action == "--get" && argc == 3)
    {
        return get(argv[2]) ? 1 : 0;
    }
    else if(action == "--set" && argc == 3)
    {
        return set(argv[2]) ? 1 : 0;
    }
    else
    {
        std::wcerr <<
            L"Usage: ampc_tagger [--version|--genres|--set file|--get file]" <<
            std::endl;
        return 1;
    }
}

// Local Variables:
// fill-column: 80
// indent-tabs-mode: nil
// End:
