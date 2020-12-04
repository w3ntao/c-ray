//
//  test_hashtable.h
//  C-ray
//
//  Created by Valtteri on 17.9.2020.
//  Copyright © 2020 Valtteri Koskivuori. All rights reserved.
//

#include "../src/utils/hashtable.h"
#include "../src/datatypes/vector.h"

bool hashtable_mixed(void) {
	bool pass = true;
	
	struct constantsDatabase *database = newConstantsDatabase();
	
	setDatabaseVector(database, "key0", (struct vector){1.0f, 2.0f, 3.0f});
	setDatabaseFloat(database, "key1", 123.4f);
	setDatabaseTag(database, "key2");
	setDatabaseString(database, "key3", "This is my cool string");
	setDatabaseInt(database, "key4", 1234);
	
	test_assert(database);
	test_assert(vecEquals(getDatabaseVector(database, "key0"), (struct vector){1.0f, 2.0f, 3.0f}));
	test_assert(getDatabaseFloat(database, "key1") == 123.4f);
	test_assert(existsInDatabase(database, "key2"));
	test_assert(stringEquals(getDatabaseString(database, "key3"), "This is my cool string"));
	test_assert(getDatabaseInt(database, "key4") == 1234);
	
	freeConstantsDatabase(database);
	
	return pass;
}

bool hashtable_fill(void) {
	bool pass = true;
	struct constantsDatabase *database = newConstantsDatabase();
	char buf[20];
	size_t iterCount = 10000;
	for (size_t i = 0; i < iterCount && pass; ++i) {
		sprintf(buf, "key%lu", i);
		setDatabaseInt(database, buf, (int)i);
	}
	test_assert(database->hashtable.elemCount == iterCount);
	size_t i;
	for (i = 0; i < iterCount && pass; ++i) {
		sprintf(buf, "key%lu", i);
		test_assert((size_t)getDatabaseInt(database, buf) == i);
	}
	freeConstantsDatabase(database);
	return pass;
}
