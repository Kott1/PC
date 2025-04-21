from locust import HttpUser, task

class WindowsStaticUser(HttpUser):

    @task
    def home(self):
        self.client.get("/home.html")

    @task
    def page(self):
        self.client.get("/page.html")
#Код для перевірки помилки неіснуючого сайту
#    @task
#    def page404(self):
#        self.client.get("/page404.html")