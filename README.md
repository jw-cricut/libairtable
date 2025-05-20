# libairtable

libairtable is an asynchronous coroutine-based C++ client for Airtable, built on asio. The CMakeLists builds the library as well as an executable which wraps the client's functionality.

Currently supports the [public REST API](https://airtable.com/api) and the [metadata API](https://airtable.com/api/meta).

See AirtableClient.hh for information on how to use the AirtableClient object, and AirtableCLI.cc for some usage examples.
