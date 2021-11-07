#include "FieldTypes.hh"

#include <phosg/Strings.hh>

using namespace std;



uint64_t parse_airtable_time(const string& time) {
  struct tm t;
  strptime(time.c_str(), "%Y-%m-%dT%H:%M:%S.000Z", &t);
  return mktime(&t);
}

string format_airtable_time(uint64_t time) {
  struct tm t;
  time_t t_secs = time;
  localtime_r(&t_secs, &t);

  string ret(0x40, '\0');
  size_t length = strftime(
      const_cast<char*>(ret.data()),
      ret.size(),
      "%Y-%m-%dT%H:%M:%S.000Z",
      &t);
  ret.resize(length);
  return ret;
}



Field::Field(ValueType type) : type(type) { }



StringField::StringField() : Field(ValueType::String) { }
StringField::StringField(const string& value)
  : Field(ValueType::String), value(value) { }
StringField::StringField(string&& value)
  : Field(ValueType::String), value(move(value)) { }
shared_ptr<JSONObject> StringField::to_json() const {
  return make_json_str(this->value);
}

IntegerField::IntegerField() : Field(ValueType::Integer), value(0) { }
IntegerField::IntegerField(int64_t value)
  : Field(ValueType::Integer), value(value) { }
shared_ptr<JSONObject> IntegerField::to_json() const {
  return make_json_int(this->value);
}

FloatField::FloatField() : Field(ValueType::Float), value(0.0) { }
FloatField::FloatField(double value)
  : Field(ValueType::Float), value(value) { }
shared_ptr<JSONObject> FloatField::to_json() const {
  return make_json_num(this->value);
}

ButtonField::ButtonField() : Field(ValueType::Button) { }
ButtonField::ButtonField(const string& url, const string& label)
  : Field(ValueType::Button), url(url), label(label) { }
ButtonField::ButtonField(string&& url, string&& label)
  : Field(ValueType::Button), url(move(url)), label(move(label)) { }
shared_ptr<JSONObject> ButtonField::to_json() const {
  unordered_map<string, shared_ptr<JSONObject>> root;
  root.emplace("url", make_json_str(this->url));
  root.emplace("label", make_json_str(this->label));
  return shared_ptr<JSONObject>(new JSONObject(move(root)));
}

CheckboxField::CheckboxField() : Field(ValueType::Checkbox), value(false) { }
CheckboxField::CheckboxField(bool value)
  : Field(ValueType::Checkbox), value(value) { }
shared_ptr<JSONObject> CheckboxField::to_json() const {
  return make_json_bool(this->value);
}

StringArrayField::StringArrayField() : Field(ValueType::StringArray) { }
StringArrayField::StringArrayField(const vector<string>& value)
  : Field(ValueType::StringArray), value(value) { }
StringArrayField::StringArrayField(vector<string>&& value)
  : Field(ValueType::StringArray), value(move(value)) { }
shared_ptr<JSONObject> StringArrayField::to_json() const {
  vector<shared_ptr<JSONObject>> ret;
  for (const auto& it : this->value) {
    ret.emplace_back(make_json_str(it));
  }
  return make_json_list(move(ret));
}

NumberArrayField::NumberArrayField() : Field(ValueType::StringArray) { }
NumberArrayField::NumberArrayField(const vector<double>& value)
  : Field(ValueType::NumberArray), value(value) { }
NumberArrayField::NumberArrayField(vector<double>&& value)
  : Field(ValueType::NumberArray), value(move(value)) { }
shared_ptr<JSONObject> NumberArrayField::to_json() const {
  vector<shared_ptr<JSONObject>> ret;
  for (const auto& it : this->value) {
    ret.emplace_back(make_json_num(it));
  }
  return make_json_list(move(ret));
}

CollaboratorField::CollaboratorField() : Field(ValueType::Collaborator) { }
CollaboratorField::CollaboratorField(
    const string& name, const string& email, const string& user_id)
  : Field(ValueType::Collaborator), name(name), email(email), user_id(user_id) { }
CollaboratorField::CollaboratorField(
    string&& name, string&& email, string&& user_id)
  : Field(ValueType::Collaborator),
    name(move(name)),
    email(move(email)),
    user_id(move(user_id)) { }
shared_ptr<JSONObject> CollaboratorField::to_json() const {
  unordered_map<string, shared_ptr<JSONObject>> root;
  root.emplace("name", make_json_str(this->name));
  root.emplace("email", make_json_str(this->email));
  root.emplace("id", make_json_str(this->user_id));
  return shared_ptr<JSONObject>(new JSONObject(move(root)));
}

MultiCollaboratorField::MultiCollaboratorField()
  : Field(ValueType::CollaboratorArray) { }
MultiCollaboratorField::MultiCollaboratorField(
    const vector<CollaboratorField>& value)
  : Field(ValueType::CollaboratorArray), value(value) { }
MultiCollaboratorField::MultiCollaboratorField(
    vector<CollaboratorField>&& value)
  : Field(ValueType::CollaboratorArray), value(move(value)) { }
shared_ptr<JSONObject> MultiCollaboratorField::to_json() const {
  vector<shared_ptr<JSONObject>> ret;
  for (const auto& it : this->value) {
    ret.emplace_back(it.to_json());
  }
  return make_json_list(move(ret));
}

Attachment::Attachment(shared_ptr<JSONObject> json) {
  this->attachment_id = json->at("id")->as_string();
  this->mime_type = json->at("type")->as_string();
  this->size = json->at("size")->as_int();
  this->filename = json->at("filename")->as_string();
  try {
    this->height = json->at("height")->as_int();
    this->width = json->at("width")->as_int();
  } catch (const JSONObject::key_error&) { }
  try {
    this->url = json->at("url")->as_string();
  } catch (const JSONObject::key_error&) { }
  try {
    auto& thumbs_json_dict = json->at("thumbnails")->as_dict();
    for (const auto& thumb_json_it : thumbs_json_dict) {
      const auto& thumb_dict = thumb_json_it.second->as_dict();
      Thumbnail& thumb = this->thumbnails[thumb_json_it.first];
      thumb.url = thumb_dict.at("url")->as_string();
      thumb.width = thumb_dict.at("width")->as_int();
      thumb.height = thumb_dict.at("height")->as_int();
    }
  } catch (const JSONObject::key_error&) { }
}

shared_ptr<JSONObject> Attachment::to_json() const {
  unordered_map<string, shared_ptr<JSONObject>> dict;
  dict.emplace("type", make_json_str(this->mime_type));
  dict.emplace("size", make_json_int(this->size));
  dict.emplace("filename", make_json_str(this->filename));
  if (this->height && this->width) {
    dict.emplace("height", make_json_int(this->height));
    dict.emplace("width", make_json_int(this->width));
  }
  if (!this->url.empty()) {
    dict.emplace("url", make_json_str(this->url));
  }
  if (!this->attachment_id.empty()) {
    dict.emplace("id", make_json_str(this->attachment_id));
  }
  if (!this->thumbnails.empty()) {
    unordered_map<string, shared_ptr<JSONObject>> thumbs_dict;
    for (const auto& thumb_it : this->thumbnails) {
      const auto& thumb = thumb_it.second;
      unordered_map<string, shared_ptr<JSONObject>> thumb_dict;
      thumb_dict.emplace("url", make_json_str(thumb.url));
      if (thumb.height && thumb.width) {
        thumb_dict.emplace("height", make_json_int(thumb.height));
        thumb_dict.emplace("width", make_json_int(thumb.width));
      }
      thumbs_dict.emplace(thumb_it.first, new JSONObject(move(thumb_dict)));
    }
    dict.emplace("thumbnails", new JSONObject(move(thumbs_dict)));
  }
  return shared_ptr<JSONObject>(new JSONObject(move(dict)));
}

AttachmentField::AttachmentField() : Field(ValueType::AttachmentArray) { }
AttachmentField::AttachmentField(const vector<Attachment>& value)
  : Field(ValueType::AttachmentArray), value(value) { }
AttachmentField::AttachmentField(vector<Attachment>&& value)
  : Field(ValueType::AttachmentArray), value(move(value)) { }
shared_ptr<JSONObject> AttachmentField::to_json() const {
  vector<shared_ptr<JSONObject>> ret;
  for (const auto& att : this->value) {
    ret.emplace_back(att.to_json());
  }
  return make_json_list(move(ret));
}



shared_ptr<Field> Record::parse_field(shared_ptr<JSONObject> json) {
  if (json->is_int()) {
    return shared_ptr<Field>(new IntegerField(json->as_int()));

  } else if (json->is_float()) {
    return shared_ptr<Field>(new FloatField(json->as_float()));

  } else if (json->is_bool()) {
    return shared_ptr<Field>(new CheckboxField(json->as_bool()));

  } else if (json->is_string()) {
    return shared_ptr<Field>(new StringField(json->as_string()));

  } else if (json->is_dict()) {
    const auto& dict = json->as_dict();
    if (dict.count("url") && dict.count("label")) {
      return shared_ptr<Field>(new ButtonField(
          dict.at("url")->as_string(), dict.at("label")->as_string()));
    } else if (dict.count("name") && dict.count("email") && dict.count("id")) {
      return shared_ptr<Field>(new CollaboratorField(
          dict.at("name")->as_string(),
          dict.at("email")->as_string(),
          dict.at("id")->as_string()));
    } else {
      throw runtime_error("unrecognized dict cell format");
    }

  } else if (json->is_list()) {
    const auto& list = json->as_list();
    if (list.empty()) {
      return shared_ptr<Field>(new StringArrayField());

    } else {
      const auto& item0 = list[0];
      if (item0->is_string()) {
        vector<string> values;
        for (const auto& item_json : list) {
          values.emplace_back(item_json->as_string());
        }
        return shared_ptr<Field>(new StringArrayField(move(values)));

      } else if (item0->is_int() || item0->is_float()) {
        vector<double> values;
        for (const auto& item_json : list) {
          values.emplace_back(item_json->as_float());
        }
        return shared_ptr<Field>(new NumberArrayField(move(values)));

      } else if (item0->is_dict()) {
        const auto& item0_dict = item0->as_dict();
        if (item0_dict.count("name") &&
            item0_dict.count("email") &&
            item0_dict.count("id")) {
          vector<CollaboratorField> values;
          for (const auto& item_json : list) {
            const auto& item_dict = item_json->as_dict();
            values.emplace_back(
                item_dict.at("name")->as_string(),
                item_dict.at("email")->as_string(),
                item_dict.at("id")->as_string());
          }
          return shared_ptr<Field>(new MultiCollaboratorField(move(values)));

        } else if (item0_dict.count("id") &&
            item0_dict.count("filename") &&
            item0_dict.count("type") &&
            item0_dict.count("url") &&
            item0_dict.count("size")) {
          vector<Attachment> values;
          for (const auto& item_json : list) {
            values.emplace_back(item_json);
          }
          return shared_ptr<Field>(new AttachmentField(move(values)));

        } else {
          throw runtime_error("unrecognized list subcell format");
        }
      } else {
        string str = json->format();
        fputs(str.c_str(), stderr);
        throw runtime_error("unrecognized list cell format");
      }
    }
  } else {
    throw runtime_error("unrecognized cell format");
  }
}

Record::Record(shared_ptr<const JSONObject> json) {
  const auto& dict = json->as_dict();
  const auto& id_from_dict = dict.at("id")->as_string();
  if (id_from_dict.size() != 17) {
    throw runtime_error("record ID length is incorrect");
  }
  strcpy(this->id, id_from_dict.c_str());
  this->creation_time = parse_airtable_time(dict.at("createdTime")->as_string());
  for (const auto& it : dict.at("fields")->as_dict()) {
    this->fields.emplace(it.first, this->parse_field(it.second));
  }
}

string Record::str() const {
  auto json = this->json_for_create();
  string json_str = json->serialize();
  string creation_time_str = format_airtable_time(this->creation_time);
  string ret = string_printf("Record(id=%s, creation_time=%s, json=",
      this->id, creation_time_str.c_str());
  ret += json_str;
  ret += ')';
  return ret;
}

shared_ptr<JSONObject> Record::json_for_create(
    const unordered_map<string, shared_ptr<Field>>& fields) {
  unordered_map<string, shared_ptr<JSONObject>> fields_dict;
  for (const auto& it : fields) {
    fields_dict.emplace(it.first, it.second->to_json());
  }

  unordered_map<string, shared_ptr<JSONObject>> root_dict;
  root_dict.emplace("fields", new JSONObject(fields_dict));
  return shared_ptr<JSONObject>(new JSONObject(move(root_dict)));
}

shared_ptr<JSONObject> Record::json_for_create() const {
  return Record::json_for_create(this->fields);
}

shared_ptr<JSONObject> Record::json_for_update() const {
  auto json = this->json_for_create();
  json->as_dict().emplace("id", new JSONObject(this->id));
  return json;
}
