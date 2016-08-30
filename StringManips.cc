#include "StringManips.hh"

string addQuotes (string tag) {
  string ret("\"");
  ret += tag;
  ret += "\"";
  return ret; 
}

string remQuotes (string tag) {
  string ret;
  bool found = false; 
  for (unsigned int i = 0; i < tag.size(); ++i) {
    char curr = tag[i];
    if (!found) {
      if (curr != '"') continue;
      found = true;
      continue;
    }

    if (curr == '"') break;
    ret += curr; 
  }

  if (!found) return tag;
  return ret; 
}
