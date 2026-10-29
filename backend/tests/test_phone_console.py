def test_phone_console_renders_server_data_without_html_injection(client):
    response = client.get("/console")

    assert response.status_code == 200
    assert "result.innerHTML" not in response.text
    assert "title.textContent = choice.name" in response.text
    assert "details.textContent" in response.text


def test_phone_console_discloses_platform_values_are_estimates(client):
    response = client.get("/console")

    assert "预计价格与送达时间，以平台为准" in response.text
