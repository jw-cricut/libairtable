#include "FieldTypes.hh"

#include <phosg/Strings.hh>

using namespace std;

uint64_t parse_airtable_time(const string& time) {
  struct tm t;
  const char* frac_start = strptime(time.c_str(), "%Y-%m-%dT%H:%M:%S.", &t);
  if (frac_start == nullptr) {
    throw runtime_error("invalid time format");
  }
  size_t frac_length = strlen(frac_start);
  if (frac_length < 2 || frac_start[frac_length - 1] != 'Z') {
    throw runtime_error("invalid time format");
  }
  frac_length--;
  if (frac_length > 6) {
    throw runtime_error("time is more precise than microseconds");
  }
  uint64_t frac = strtoull(frac_start, nullptr, 10);
  for (size_t place = frac_length; place < 6; place++) {
    frac *= 10;
  }
  return mktime(&t) * 1000000 + frac;
}

string format_airtable_time(uint64_t time) {
  struct tm t;
  time_t t_secs = time;
  localtime_r(&t_secs, &t);

  string str(0x40, '\0');
  str.resize(strftime(const_cast<char*>(str.data()), str.size(), "%Y-%m-%dT%H:%M:%S", &t));
  return std::format("{}.{:03}Z", str, (time % 1000000) / 1000);
}

Field::Field(ValueType type) : type(type) {}

StringField::StringField() : Field(ValueType::String) {}
StringField::StringField(const string& value) : Field(ValueType::String), value(value) {}
StringField::StringField(string&& value) : Field(ValueType::String), value(std::move(value)) {}
phosg::JSON StringField::to_json() const {
  return this->value;
}

IntegerField::IntegerField() : Field(ValueType::Integer), value(0) {}
IntegerField::IntegerField(int64_t value) : Field(ValueType::Integer), value(value) {}
phosg::JSON IntegerField::to_json() const {
  return this->value;
}

FloatField::FloatField() : Field(ValueType::Float), value(0.0) {}
FloatField::FloatField(double value) : Field(ValueType::Float), value(value) {}
phosg::JSON FloatField::to_json() const {
  return this->value;
}

ButtonField::ButtonField() : Field(ValueType::Button) {}
ButtonField::ButtonField(const string& url, const string& label) : Field(ValueType::Button), url(url), label(label) {}
ButtonField::ButtonField(string&& url, string&& label) : Field(ValueType::Button), url(std::move(url)), label(std::move(label)) {}
phosg::JSON ButtonField::to_json() const {
  return phosg::JSON::dict({{"url", this->url}, {"label", this->label}});
}

CheckboxField::CheckboxField() : Field(ValueType::Checkbox), value(false) {}
CheckboxField::CheckboxField(bool value) : Field(ValueType::Checkbox), value(value) {}
phosg::JSON CheckboxField::to_json() const {
  return this->value;
}

StringArrayField::StringArrayField() : Field(ValueType::StringArray) {}
StringArrayField::StringArrayField(const vector<string>& value) : Field(ValueType::StringArray), value(value) {}
StringArrayField::StringArrayField(vector<string>&& value) : Field(ValueType::StringArray), value(std::move(value)) {}
phosg::JSON StringArrayField::to_json() const {
  auto ret = phosg::JSON::list();
  for (const auto& it : this->value) {
    ret.emplace_back(it);
  }
  return ret;
}

NumberArrayField::NumberArrayField() : Field(ValueType::StringArray) {}
NumberArrayField::NumberArrayField(const vector<double>& value) : Field(ValueType::NumberArray), value(value) {}
NumberArrayField::NumberArrayField(vector<double>&& value) : Field(ValueType::NumberArray), value(std::move(value)) {}
phosg::JSON NumberArrayField::to_json() const {
  auto ret = phosg::JSON::list();
  for (const auto& it : this->value) {
    ret.emplace_back(it);
  }
  return ret;
}

CollaboratorField::CollaboratorField() : Field(ValueType::Collaborator) {}
CollaboratorField::CollaboratorField(const string& name, const string& email, const string& user_id)
    : Field(ValueType::Collaborator), name(name), email(email), user_id(user_id) {}
CollaboratorField::CollaboratorField(string&& name, string&& email, string&& user_id)
    : Field(ValueType::Collaborator),
      name(std::move(name)),
      email(std::move(email)),
      user_id(std::move(user_id)) {}
phosg::JSON CollaboratorField::to_json() const {
  return phosg::JSON::dict({
      {"name", this->name},
      {"email", this->email},
      {"id", this->user_id},
  });
}

MultiCollaboratorField::MultiCollaboratorField() : Field(ValueType::CollaboratorArray) {}
MultiCollaboratorField::MultiCollaboratorField(const vector<CollaboratorField>& value)
    : Field(ValueType::CollaboratorArray), value(value) {}
MultiCollaboratorField::MultiCollaboratorField(vector<CollaboratorField>&& value)
    : Field(ValueType::CollaboratorArray), value(std::move(value)) {}
phosg::JSON MultiCollaboratorField::to_json() const {
  auto ret = phosg::JSON::list();
  for (const auto& it : this->value) {
    ret.emplace_back(it.to_json());
  }
  return ret;
}

Attachment::Attachment(const phosg::JSON& json) {
  this->attachment_id = json.at("id").as_string();
  this->mime_type = json.at("type").as_string();
  this->size = json.at("size").as_int();
  this->filename = json.at("filename").as_string();
  try {
    this->height = json.at("height").as_int();
    this->width = json.at("width").as_int();
  } catch (const std::runtime_error&) {
  }
  try {
    this->url = json.at("url").as_string();
  } catch (const std::runtime_error&) {
  }
  try {
    auto& thumbs_json_dict = json.at("thumbnails").as_dict();
    for (const auto& [name, thumb_json_it] : thumbs_json_dict) {
      Thumbnail& thumb = this->thumbnails[name];
      thumb.url = thumb_json_it->at("url").as_string();
      thumb.width = thumb_json_it->at("width").as_int();
      thumb.height = thumb_json_it->at("height").as_int();
    }
  } catch (const std::runtime_error&) {
  }
}

phosg::JSON Attachment::to_json() const {
  auto dict = phosg::JSON::dict({
      {"type", this->mime_type},
      {"size", this->size},
      {"filename", this->filename},
  });
  if (this->height && this->width) {
    dict.emplace("height", this->height);
    dict.emplace("width", this->width);
  }
  if (!this->url.empty()) {
    dict.emplace("url", this->url);
  }
  if (!this->attachment_id.empty()) {
    dict.emplace("id", this->attachment_id);
  }
  if (!this->thumbnails.empty()) {
    auto thumbs_dict = phosg::JSON::dict();
    for (const auto& thumb_it : this->thumbnails) {
      const auto& thumb = thumb_it.second;
      auto thumb_dict = phosg::JSON::dict({{"url", thumb.url}});
      if (thumb.height && thumb.width) {
        thumb_dict.emplace("height", thumb.height);
        thumb_dict.emplace("width", thumb.width);
      }
      thumbs_dict.emplace(thumb_it.first, std::move(thumb_dict));
    }
    dict.emplace("thumbnails", std::move(thumbs_dict));
  }
  return dict;
}

AttachmentField::AttachmentField() : Field(ValueType::AttachmentArray) {}
AttachmentField::AttachmentField(const vector<Attachment>& value) : Field(ValueType::AttachmentArray), value(value) {}
AttachmentField::AttachmentField(vector<Attachment>&& value) : Field(ValueType::AttachmentArray), value(std::move(value)) {}
phosg::JSON AttachmentField::to_json() const {
  auto ret = phosg::JSON::list();
  for (const auto& att : this->value) {
    ret.emplace_back(att.to_json());
  }
  return ret;
}

shared_ptr<Field> Record::parse_field(const phosg::JSON& json) {
  if (json.is_int()) {
    return make_shared<IntegerField>(json.as_int());

  } else if (json.is_float()) {
    return make_shared<FloatField>(json.as_float());

  } else if (json.is_bool()) {
    return make_shared<CheckboxField>(json.as_bool());

  } else if (json.is_string()) {
    return make_shared<StringField>(json.as_string());

  } else if (json.is_dict()) {
    const auto& dict = json.as_dict();
    if (dict.count("url") && dict.count("label")) {
      return make_shared<ButtonField>(dict.at("url")->as_string(), dict.at("label")->as_string());
    } else if (dict.count("name") && dict.count("email") && dict.count("id")) {
      return make_shared<CollaboratorField>(dict.at("name")->as_string(), dict.at("email")->as_string(), dict.at("id")->as_string());
    } else {
      throw runtime_error("unrecognized dict cell format");
    }

  } else if (json.is_list()) {
    const auto& list = json.as_list();
    if (list.empty()) {
      return make_shared<StringArrayField>();

    } else {
      const auto& item0 = list[0];
      if (item0->is_string()) {
        vector<string> values;
        for (const auto& item_json : list) {
          values.emplace_back(item_json->as_string());
        }
        return make_shared<StringArrayField>(std::move(values));

      } else if (item0->is_int() || item0->is_float()) {
        vector<double> values;
        for (const auto& item_json : list) {
          values.emplace_back(item_json->as_float());
        }
        return make_shared<NumberArrayField>(std::move(values));

      } else if (item0->is_dict()) {
        const auto& item0_dict = item0->as_dict();
        if (item0_dict.count("name") && item0_dict.count("email") && item0_dict.count("id")) {
          vector<CollaboratorField> values;
          for (const auto& item_json : list) {
            const auto& item_dict = item_json->as_dict();
            values.emplace_back(item_dict.at("name")->as_string(), item_dict.at("email")->as_string(), item_dict.at("id")->as_string());
          }
          return make_shared<MultiCollaboratorField>(std::move(values));

        } else if (item0_dict.count("id") &&
            item0_dict.count("filename") &&
            item0_dict.count("type") &&
            item0_dict.count("url") &&
            item0_dict.count("size")) {
          vector<Attachment> values;
          for (const auto& item_json : list) {
            values.emplace_back(*item_json);
          }
          return make_shared<AttachmentField>(std::move(values));

        } else {
          throw runtime_error("Unrecognized list subcell format");
        }
      } else {
        throw runtime_error("Unrecognized list cell format");
      }
    }
  } else {
    throw runtime_error("Unrecognized cell format");
  }
}

Record::Record(const phosg::JSON& json) {
  const auto& dict = json.as_dict();
  const auto& id_from_dict = dict.at("id")->as_string();
  if (id_from_dict.size() != 17) {
    throw runtime_error("Record ID length is incorrect");
  }
  strcpy(this->id, id_from_dict.c_str());
  this->creation_time = parse_airtable_time(dict.at("createdTime")->as_string());
  for (const auto& it : dict.at("fields")->as_dict()) {
    this->fields.emplace(it.first, this->parse_field(*it.second));
  }
}

string Record::str() const {
  return std::format("Record(id={}, creation_time={}, json={})",
      this->id, format_airtable_time(this->creation_time), this->json_for_create().serialize());
}

phosg::JSON Record::json_for_create(const unordered_map<string, shared_ptr<Field>>& fields) {
  auto fields_dict = phosg::JSON::dict();
  for (const auto& it : fields) {
    fields_dict.emplace(it.first, it.second->to_json());
  }
  return phosg::JSON::dict({{"fields", std::move(fields_dict)}});
}

phosg::JSON Record::json_for_create() const {
  return Record::json_for_create(this->fields);
}

phosg::JSON Record::json_for_update() const {
  auto json = this->json_for_create();
  json.emplace("id", this->id);
  return json;
}
