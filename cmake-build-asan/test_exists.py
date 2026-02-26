def mock_exists(name):
    return name in globals()

print('DEBUG mock_exists:', mock_exists('_have_functions'))
_have_functions = ['access']
print('DEBUG mock_exists after:', mock_exists('_have_functions'))
