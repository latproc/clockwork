# An example of using WebRequest to fetch data from a web API
#
# Configure the URL paameters and send test.test to run the example.

UrlParameters MACHINE {
  OPTION root_url "https://localhost:8080/api?";
  OPTION action "sample_action";
  OPTION key "myapikey";
  OPTION id "user123";
}

Monitor MACHINE requester, test {
  OPTION timeout 2000;
  abort WHEN test.requester IS Running AND test.TIMER > timeout;
  idle DEFAULT;
  ENTER abort { SEND stop TO requester; }
}

Test MACHINE params {
  OPTION duration 5;
	requester WebRequest;
  data LIST;
  OPTION details JSON_VALUE [];

  COMMAND set_data {
    details := ITEM ${userDetails} OF requester.Result;
    PUSH ITEMS FROM details TO data;
    SEND reset TO requester;
  }

	update WHEN SELF IS idle AND TIMER > 1000;
	idle DEFAULT;

	COMMAND test {
    requester.Request :=
        params.root_url +
        "action=" + params.action +
        "&key=" + params.key +
        "&id=" + params.id;
		SEND start TO requester;
	}
}

url UrlParameters;
test Test url;
monitor Monitor test.requester, test;
