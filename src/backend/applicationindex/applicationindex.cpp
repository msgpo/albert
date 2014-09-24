// albert - a simple application launcher for linux
// Copyright (C) 2014 Manuel Schneider
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "applicationindex.h"
#include "settings.h"
#include "boost/algorithm/string.hpp"
#include "boost/filesystem.hpp"
#include <functional>
#include <fstream>
#include <algorithm>
#include <unistd.h>
#include "websearch/websearch.h"

//REMOVE
#include <iostream>

/**************************************************************************//**
 * @brief ApplicationIndex::ApplicationIndex
 */
ApplicationIndex::ApplicationIndex(){
	_indexFile = Settings::instance()->configDir() + "idx_apps";
}

/**************************************************************************//**
 * @brief ApplicationIndex::buildIndex
 */
void ApplicationIndex::buildIndex()
{
	// If there is a serialized index use it
	std::ifstream f(_indexFile);
	if (f.good()){
		boost::archive::text_iarchive ia(f);
		ia.template register_type<Item>();
		ia >> _index;
		f.close();
	}
	else
	{
		std::string paths = Settings::instance()->get("app_index_paths");
		std::cout << "[ApplicationIndex] Looking in: " << paths << std::endl;
		std::vector<std::string> pathList;
		boost::split(pathList, paths, boost::is_any_of(":,"), boost::token_compress_on);

		// Define a lambda for recursion
		std::function<void(const boost::filesystem::path &p)> rec_dirsearch = [&] (const boost::filesystem::path &p)
		{
			boost::filesystem::path path(p);
			boost::filesystem::directory_iterator end_iterator;
			if ( boost::filesystem::exists(path))
			{
				if (boost::filesystem::is_directory(path))
				{
					for( boost::filesystem::directory_iterator d(path); d != end_iterator; ++d)
						rec_dirsearch(*d);
				}
				if (boost::filesystem::is_regular_file(path))
				{
					// Check extension
					if (path.extension().compare(".desktop"))
						return;

					// Read the entries in the desktopfile
					std::map<std::string, std::string> desktopfile;
					std::ifstream file(path.string());
					if (!file.good())
						return;
					std::string str;
					while (std::getline(file, str))
					{
						std::size_t found = str.find_first_of('=');
						if (found == std::string::npos || found == str.length())
							continue;
						desktopfile[str.substr(0,found)] = str.substr(found+1);
					}

					// Check if this shall be displayed
					std::string noDisplay = desktopfile["NoDisplay"];
					std::transform(noDisplay.begin(), noDisplay.end(), noDisplay.begin(),
								   std::bind2nd(std::ptr_fun(&std::tolower<char>), Settings::instance()->locale()));
					if (noDisplay == "true")
						return;

					// Check if this shall be runned in terminal
					std::string term = desktopfile["Terminal"];
					std::transform(term.begin(), term.end(), term.begin(),
								   std::bind2nd(std::ptr_fun(&std::tolower<char>), Settings::instance()->locale()));

					// Strip every desktop file params
					std::string exec = desktopfile["Exec"];
					size_t d = exec.find_first_of('%');
					if (d != std::string::npos){
						d = exec.find_first_of(' ');
						exec.resize(d);
					}

					_index.push_back(
						new Item(
							desktopfile["Name"],
							(desktopfile["Comment"].empty())?desktopfile["GenericName"]:desktopfile["Comment"],
							desktopfile["Icon"],
							exec,
							(term=="true")?true:false
						)
					);
				}
			}
		};

		// Finally do this recursion for all paths
		for ( std::string &p : pathList)
			rec_dirsearch(boost::filesystem::path(p));
		std::sort(_index.begin(), _index.end(), CaseInsensitiveCompare(Settings::instance()->locale()));
	}

	std::cout << "[ApplicationIndex] Indexing done. Found " << _index.size() << " apps." << std::endl;
}

void ApplicationIndex::saveIndex() const
{
	std::ofstream f(_indexFile);
	boost::archive::text_oarchive oa(f);
	oa.template register_type<Item>();
	oa << _index;
	f.close();
}


/*****************************************************************************/
/*****************************************************************************/
/************************** ApplicationIndexItem *****************************/
/*****************************************************************************/
/**************************************************************************//**
 * @brief ApplicationIndex::ApplicationIndexItem::action
 * @param a
 */
void ApplicationIndex::Item::action(Action a)
{
	_lastAccess = std::chrono::system_clock::now().time_since_epoch().count();

	pid_t pid;
	switch (a) {
	case Action::Enter:
		pid = fork();
		if (pid == 0) {
			pid_t sid = setsid();
			if (sid < 0) exit(EXIT_FAILURE);
			if (_term)
				execlp("konsole", "konsole" , "-e", _exec.c_str(), (char *)0);
			else
				execlp(_exec.c_str(), _exec.c_str(), (char *)0);
			exit(1);
		}
		break;
	case Action::Alt:
		pid = fork();
		if (pid == 0) {
			pid_t sid = setsid();
			if (sid < 0) exit(EXIT_FAILURE);
			if (_term)
				execlp("kdesu", "kdesu", std::string("konsole -e "+_exec).c_str(), (char *)0);
			else
				execlp("kdesu", "kdesu", _exec.c_str(), (char *)0);
			exit(1);
		}
		break;
	case Action::Ctrl:
		WebSearch::instance()->defaultSearch(_name);
		break;
	}
}

/**************************************************************************//**
 * @brief ApplicationIndex::ApplicationIndexItem::actionText
 * @param a
 * @return
 */
std::string ApplicationIndex::Item::actionText(Action a) const
{
	switch (a) {
	case Action::Enter:
		return "Start " + _name;
		break;
	case Action::Alt:
		return "Start " + _name + " as root";
		break;
	case Action::Ctrl:
		return WebSearch::instance()->defaultSearchText(_name);
		break;
	}
	// Will never happen
	return "";
}

