
(define-module (gnucash report iframe-url))
(use-modules (gnucash main)) ;; FIXME: delete after we finish modularizing.
(use-modules (ice-9 slib))
(use-modules (gnucash gnc-module))

(gnc:module-load "gnucash/report/report-system" 0)

(define (options-generator)
  (let ((options (gnc:new-options)))
    (gnc:register-option options
                         (gnc:make-string-option 
                          (N_ "General") 
                          (N_ "URL to frame")
                          "a" (N_ "URL to display in report")
                          "http://www.gnucash.org"))
    options))

(define (renderer report-obj)
  (let ((url (gnc:option-value
              (gnc:lookup-option
               (gnc:report-options report-obj)
               (N_ "General") (N_ "URL to frame"))))
        (doc (gnc:make-html-document))
        (txt (gnc:make-html-text)))
    (gnc:html-text-append!
     txt 
     (gnc:html-markup/attr 
      "iframe" (format #f "src=\"~A\"" url)
      "Your browser does not support inline frames, sorry."))
    (gnc:html-document-add-object! doc txt)
    doc))

(gnc:define-report 
 'version 1
 'name (N_ "Frame URL")
 'menu-name (N_ "Custom Web Report")
 'menu-path (list gnc:menuname-utility)
 'options-generator options-generator
 'renderer renderer)