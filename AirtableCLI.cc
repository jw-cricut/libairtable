#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <set>

#include "AirtableClient.hh"

using namespace std;



void print_usage() {
  fputs("\
Usage: airtable <command> [options]\n\
\n\
Commands:\n\
  list-bases: Lists up to 1000 bases accessible using the given API key.\n\
    Requires a client secret to be set in .airtablerc.json.\n\
\n\
  get-base-schema BASE-ID: Gets the schema of all tables in the given base.\n\
    Requires a client secret to be set in .airtablerc.json.\n\
\n\
  list-records BASE-ID TABLE-NAME-OR-ID [options]: Lists records in a table.\n\
    Options:\n\
    --include-field=NAME-OR-ID: If given, only return data in this column. May\n\
        be given multiple times.\n\
    --filter-formula=FORMULA: If given, only return records for which this\n\
        formula returns a truthy value.\n\
    --max-records=N: Return at most this many records.\n\
    --sort-field-asc=FIELD-NAME-OR-ID: Sort the returned records by this field\n\
        in ascending order. May be given multiple times.\n\
    --sort-field-desc=FIELD-NAME-OR-ID: Sort the returned records by this field\n\
        in descending order. May be given multiple times.\n\
    --view=NAME-OR-ID: Only return records that are visible in this view.\n\
\n\
  get-record BASE-ID TABLE-NAME-OR-ID RECORD-ID: Gets the contents of a\n\
    specific record.\n\
\n\
  create-records BASE-ID TABLE-NAME-OR-ID [JSON]: Creates new records. Input\n\
    JSON should be of the form [{field_name_or_id: value, ...}, ...]. If JSON\n\
    is not given on the command line, it is read from stdin instead.\n\
\n\
  update-records BASE-ID TABLE-NAME-OR-ID [JSON]: Updates existing records.\n\
    Input JSON should be of the form {record_id: {field_name_or_id: value,\n\
    ...}, ...}. If JSON is not given on the command line, it is read from stdin\n\
    instead.\n\
\n\
  delete-records BASE-ID TABLE-NAME-OR-ID RECORD-ID [RECORD-ID ...]: Deletes\n\
    records.\n\
\n\
", stderr);
}



enum class Command {
  ListBases,
  GetBaseSchema,
  ListRecords,
  GetRecord,
  CreateRecords,
  UpdateRecords,
  DeleteRecords,
};

void write_json(shared_ptr<JSONObject> json) {
  string output = json->format();
  fwritex(stdout, output);
  fputc('\n', stdout);
}

void output_records_list(vector<Record> records) {
  vector<shared_ptr<JSONObject>> record_jsons;
  for (const auto& record : records) {
    shared_ptr<JSONObject> record_json = record.json_for_update();
    record_json->as_dict().emplace("creation_time", make_json_str(
        format_airtable_time(record.creation_time)));
    record_jsons.emplace_back(record_json);
  }
  shared_ptr<JSONObject> record_list_json(new JSONObject(move(record_jsons)));
  write_json(record_list_json);
}

DetachedTask run_command(
    EventBase& base,
    const string& api_key,
    const string& client_secret,
    int argc,
    char** argv) {
  if (argc < 2) {
    print_usage();
    throw invalid_argument("not enough arguments");
  }

  EvDNSBase dns_base(base);
  AirtableClient client(base, dns_base, api_key);

  Command command = Command::ListRecords;
  AirtableClient::ListRecordsOptions list_records_options;
  string base_id;
  string table_id;
  string input_json_str;
  vector<string> record_ids;
  if (!strcmp(argv[1], "list-bases")) {
    command = Command::ListBases;
    if (argc != 2) {
      throw invalid_argument("list-bases does not take any arguments");
    }

  } else if (!strcmp(argv[1], "get-base-schema")) {
    command = Command::GetBaseSchema;
    if (argc != 3) {
      throw invalid_argument("get-base-schema requires a base id");
    }
    base_id = argv[2];

  } else if (!strcmp(argv[1], "list-records")) {
    command = Command::ListRecords;
    if (argc < 4) {
      throw invalid_argument("list-records requires a base id and table id");
    }
    base_id = argv[2];
    table_id = argv[3];
    for (int x = 4; x < argc; x++) {
      if (!strncmp(argv[x], "--include-field=", 16)) {
        list_records_options.fields.emplace_back(&argv[x][16]);
      } else if (!strncmp(argv[x], "--filter-formula=", 17)) {
        list_records_options.filter_formula = &argv[x][17];
      } else if (!strncmp(argv[x], "--max_records=", 14)) {
        list_records_options.max_records = strtoull(&argv[x][14], nullptr, 0);
      } else if (!strncmp(argv[x], "--sort-field-asc=", 17)) {
        list_records_options.sort_fields.emplace_back(make_pair(&argv[x][17], true));
      } else if (!strncmp(argv[x], "--sort-field-desc=", 18)) {
        list_records_options.sort_fields.emplace_back(make_pair(&argv[x][18], false));
      } else if (!strncmp(argv[x], "--view=", 7)) {
        list_records_options.view = &argv[x][7];
      } else {
        throw invalid_argument("unknown option");
      }
    }

  } else if (!strcmp(argv[1], "get-record")) {
    command = Command::GetRecord;
    if (argc != 5) {
      throw invalid_argument("get-record requires a base id, table id, and record id");
    }
    base_id = argv[2];
    table_id = argv[3];
    record_ids.emplace_back(argv[4]);

  } else if (!strcmp(argv[1], "create-records")) {
    command = Command::CreateRecords;
    if (argc < 4 || argc > 5) {
      throw invalid_argument("create-records requires a base id and table id and optional json");
    }
    base_id = argv[2];
    table_id = argv[3];
    if (argc == 5) {
      input_json_str = argv[4];
    }

  } else if (!strcmp(argv[1], "update-records")) {
    command = Command::UpdateRecords;
    if (argc < 4 || argc > 5) {
      throw invalid_argument("update-records requires a base id and table id and optional json");
    }
    base_id = argv[2];
    table_id = argv[3];
    if (argc == 5) {
      input_json_str = argv[4];
    }

  } else if (!strcmp(argv[1], "delete-records")) {
    command = Command::DeleteRecords;
    if (argc < 5) {
      throw invalid_argument("list-records requires a base id, table id, and at least one record id");
    }
    base_id = argv[2];
    table_id = argv[3];
    for (int x = 4; x < argc; x++) {
      record_ids.emplace_back(argv[x]);
    }

  } else {
    throw invalid_argument("invalid command");
  }

  switch (command) {
    case Command::ListBases: {
      auto base_infos = co_await client.list_bases();

      vector<shared_ptr<JSONObject>> base_jsons;
      for (const auto& base_info : base_infos) {
        JSONObject::dict_type base_dict;
        base_dict.emplace("id", make_json_str(base_info.base_id));
        base_dict.emplace("name", make_json_str(base_info.base_id));
        base_dict.emplace("permission_level", make_json_str(base_info.base_id));
        base_jsons.emplace_back(new JSONObject(move(base_dict)));
      }
      auto output_json = make_json_list(move(base_jsons));
      write_json(output_json);
      break;
    }

    case Command::GetBaseSchema: {
      auto table_schemas = co_await client.get_base_schema(base_id);
      // TODO
      throw runtime_error("get-base-schema is not yet implemented");
      break;
    }

    case Command::ListRecords: {
      auto records = co_await client.list_records(
          base_id, table_id, &list_records_options);
      output_records_list(records);
      break;
    }

    case Command::GetRecord: {
      auto record = co_await client.get_record(
          base_id, table_id, record_ids.at(0));

      string creation_time = format_airtable_time(record.creation_time);
      shared_ptr<JSONObject> record_json = record.json_for_update();
      record_json->as_dict().emplace("creation_time", make_json_str(
          creation_time));
      write_json(record_json);
      break;
    }

    case Command::CreateRecords: {
      if (input_json_str.empty()) {
        input_json_str = read_all(stdin);
      }
      auto input_json = JSONObject::parse(input_json_str);

      vector<unordered_map<string, shared_ptr<Field>>> records_contents;
      for (const auto& record_contents_json : input_json->as_list()) {
        unordered_map<string, shared_ptr<Field>> record_contents;
        for (const auto& cell_json_it : record_contents_json->as_dict()) {
          record_contents.emplace(
              cell_json_it.first, Record::parse_field(cell_json_it.second));
        }
        records_contents.emplace_back(move(record_contents));
      }

      auto record_ids = co_await client.create_records(
          base_id, table_id, records_contents);

      fputs("[\n", stdout);
      for (size_t x = 0; x < record_ids.size(); x++) {
        fprintf(stdout, "  \"%s\"%s\n", record_ids[x].c_str(),
            (x == record_ids.size() - 1) ? "" : ",");
      }
      fputs("]\n", stdout);
      break;
    }

    case Command::UpdateRecords: {
      if (input_json_str.empty()) {
        input_json_str = read_all(stdin);
      }
      auto input_json = JSONObject::parse(input_json_str);

      unordered_map<string, unordered_map<string, shared_ptr<Field>>> records_contents;
      for (const auto& record_json_it : input_json->as_dict()) {
        unordered_map<string, shared_ptr<Field>> record_contents;
        for (const auto& cell_json_it : record_json_it.second->as_dict()) {
          record_contents.emplace(
              cell_json_it.first, Record::parse_field(cell_json_it.second));
        }
        records_contents.emplace(record_json_it.first, move(record_contents));
      }

      auto records = co_await client.update_records(
          base_id, table_id, records_contents);
      output_records_list(records);
      break;
    }

    case Command::DeleteRecords: {
      auto results = co_await client.delete_records(
          base_id, table_id, record_ids);

      fputs("{\n", stdout);
      size_t num_written = 0;
      for (const auto& it : results) {
        fprintf(stdout, "  \"%s\": %s%s\n",
            it.first.c_str(), it.second ? "true" : "false",
            (num_written == results.size() - 1) ? "" : ",");
        num_written++;
      }
      fputs("}\n", stdout);
      break;
    }

    default:
      throw invalid_argument("invalid command");
  }
}

int main(int argc, char** argv) {
  string api_key;
  string client_secret;
  string rc_filename = get_user_home_directory() + "/.airtablerc.json";
  try {
    string rc_contents = load_file(rc_filename);
    auto json = JSONObject::parse(rc_contents);
    api_key = json->at("api_key")->as_string();
    client_secret = json->at("client_secret")->as_string();
  } catch (const cannot_open_file&) {
    save_file(rc_filename, "\
{\n\
  \"api_key\": \"\", // TODO: fill this in\n\
  \"client_secret\": \"\", // TODO: fill this in\n\
}\n");
    fprintf(stderr, "%s does not exist; it was created with default values. Edit the file to fill in the required fields, then run this program again.\n",
        rc_filename.c_str());
    return 1;
  }
  if (api_key.size() != 17 || !starts_with(api_key, "key")) {
    fprintf(stderr, "%s does not contain an appropriate Airtable API key.\n",
        rc_filename.c_str());
    return 1;
  }

  SSL_library_init();
  ERR_load_crypto_strings();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();

  EventBase base;
  run_command(base, api_key, client_secret, argc, argv);
  base.run();

  return 0;
}
